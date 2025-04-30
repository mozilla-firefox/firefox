/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * SSL3 Protocol
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* TODO(ekr): Implement HelloVerifyRequest on server side. OK for now. */

#include "cert.h"
#include "ssl.h"
#include "cryptohi.h" /* for DSAU_ stuff */
#include "keyhi.h"
#include "secder.h"
#include "secitem.h"
#include "sechash.h"

#include "sslimpl.h"
#include "sslproto.h"
#include "sslerr.h"
#include "ssl3ext.h"
#include "ssl3exthandle.h"
#include "tls13ech.h"
#include "tls13exthandle.h"
#include "tls13psk.h"
#include "tls13subcerts.h"
#include "prtime.h"
#include "prinrval.h"
#include "prerror.h"
#include "pratom.h"
#include "prthread.h"
#include "nss.h"
#include "nssoptions.h"

#include "pk11func.h"
#include "secmod.h"
#include "blapi.h"

#include <limits.h>
#include <stdio.h>

static PK11SymKey *ssl3_GenerateRSAPMS(sslSocket *ss, ssl3CipherSpec *spec,
                                       PK11SlotInfo *serverKeySlot);
static SECStatus ssl3_ComputeMasterSecret(sslSocket *ss, PK11SymKey *pms,
                                          PK11SymKey **msp);
static SECStatus ssl3_DeriveConnectionKeys(sslSocket *ss,
                                           PK11SymKey *masterSecret);
static SECStatus ssl3_HandshakeFailure(sslSocket *ss);
static SECStatus ssl3_SendCertificate(sslSocket *ss);
static SECStatus ssl3_SendCertificateRequest(sslSocket *ss);
static SECStatus ssl3_SendNextProto(sslSocket *ss);
static SECStatus ssl3_SendFinished(sslSocket *ss, PRInt32 flags);
static SECStatus ssl3_SendServerHelloDone(sslSocket *ss);
static SECStatus ssl3_SendServerKeyExchange(sslSocket *ss);
static SECStatus ssl3_HandleClientHelloPart2(sslSocket *ss,
                                             SECItem *suites,
                                             sslSessionID *sid,
                                             const PRUint8 *msg,
                                             unsigned int len);
static SECStatus ssl3_HandleServerHelloPart2(sslSocket *ss,
                                             const SECItem *sidBytes,
                                             int *retErrCode);
static SECStatus ssl3_HandlePostHelloHandshakeMessage(sslSocket *ss,
                                                      PRUint8 *b,
                                                      PRUint32 length);
static SECStatus ssl3_FlushHandshakeMessages(sslSocket *ss, PRInt32 flags);
static CK_MECHANISM_TYPE ssl3_GetHashMechanismByHashType(SSLHashType hashType);
static CK_MECHANISM_TYPE ssl3_GetMgfMechanismByHashType(SSLHashType hash);
PRBool ssl_IsRsaPssSignatureScheme(SSLSignatureScheme scheme);
PRBool ssl_IsRsaeSignatureScheme(SSLSignatureScheme scheme);
PRBool ssl_IsRsaPkcs1SignatureScheme(SSLSignatureScheme scheme);
PRBool ssl_IsDsaSignatureScheme(SSLSignatureScheme scheme);
static SECStatus ssl3_UpdateDefaultHandshakeHashes(sslSocket *ss,
                                                   const unsigned char *b,
                                                   unsigned int l);
const PRUint32 kSSLSigSchemePolicy =
    NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_ANY_SIGNATURE;

const PRUint8 ssl_hello_retry_random[] = {
    0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11,
    0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
    0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,
    0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C
};
PR_STATIC_ASSERT(PR_ARRAY_SIZE(ssl_hello_retry_random) == SSL3_RANDOM_LENGTH);

/* This list of SSL3 cipher suites is sorted in descending order of
 * precedence (desirability).  It only includes cipher suites we implement.
 * This table is modified by SSL3_SetPolicy(). The ordering of cipher suites
 * in this table must match the ordering in SSL_ImplementedCiphers (sslenum.c)
 *
 * Important: See bug 946147 before enabling, reordering, or adding any cipher
 * suites to this list.
 */
/* clang-format off */
static ssl3CipherSuiteCfg cipherSuites[ssl_V3_SUITES_IMPLEMENTED] = {
   /*      cipher_suite                     policy       enabled   isPresent */
 /* Special TLS 1.3 suites. */
 { TLS_AES_128_GCM_SHA256, SSL_ALLOWED, PR_TRUE, PR_FALSE },
 { TLS_CHACHA20_POLY1305_SHA256, SSL_ALLOWED, PR_TRUE, PR_FALSE },
 { TLS_AES_256_GCM_SHA384, SSL_ALLOWED, PR_TRUE, PR_FALSE },

 { TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,   SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,   SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,   SSL_ALLOWED, PR_TRUE, PR_FALSE},
   /* TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA is out of order to work around
    * bug 946147.
    */
 { TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,    SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,    SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,      SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256, SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,   SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,      SSL_ALLOWED, PR_TRUE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384, SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA,   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDHE_ECDSA_WITH_RC4_128_SHA,        SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_RC4_128_SHA,          SSL_ALLOWED, PR_FALSE, PR_FALSE},

 { TLS_DHE_RSA_WITH_AES_128_GCM_SHA256,     SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256,SSL_ALLOWED,PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_AES_128_GCM_SHA256,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,     SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_AES_256_GCM_SHA384,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_RSA_WITH_AES_128_CBC_SHA,        SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_AES_128_CBC_SHA,        SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_RSA_WITH_AES_128_CBC_SHA256,     SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_AES_128_CBC_SHA256,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA,   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA,   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_RSA_WITH_AES_256_CBC_SHA,        SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_AES_256_CBC_SHA,        SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_RSA_WITH_AES_256_CBC_SHA256,     SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_AES_256_CBC_SHA256,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA,   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA,   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,       SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA,       SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_DHE_DSS_WITH_RC4_128_SHA,            SSL_ALLOWED, PR_FALSE, PR_FALSE},

 { TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_RSA_WITH_AES_128_CBC_SHA,       SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA,     SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_RSA_WITH_AES_256_CBC_SHA,       SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA,    SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA,      SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_ECDSA_WITH_RC4_128_SHA,         SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_RSA_WITH_RC4_128_SHA,           SSL_ALLOWED, PR_FALSE, PR_FALSE},

 /* RSA */
 { TLS_RSA_WITH_AES_128_GCM_SHA256,         SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_AES_256_GCM_SHA384,         SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_AES_128_CBC_SHA,            SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_AES_128_CBC_SHA256,         SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_CAMELLIA_128_CBC_SHA,       SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_AES_256_CBC_SHA,            SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_AES_256_CBC_SHA256,         SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_CAMELLIA_256_CBC_SHA,       SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_SEED_CBC_SHA,               SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_3DES_EDE_CBC_SHA,           SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_RC4_128_SHA,                SSL_ALLOWED, PR_TRUE,  PR_FALSE},
 { TLS_RSA_WITH_RC4_128_MD5,                SSL_ALLOWED, PR_TRUE,  PR_FALSE},

 /* 56-bit DES "domestic" cipher suites */
 { TLS_DHE_RSA_WITH_DES_CBC_SHA,            SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_DHE_DSS_WITH_DES_CBC_SHA,            SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_DES_CBC_SHA,                SSL_ALLOWED, PR_FALSE, PR_FALSE},

 /* ciphersuites with no encryption */
 { TLS_ECDHE_ECDSA_WITH_NULL_SHA,           SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDHE_RSA_WITH_NULL_SHA,             SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_RSA_WITH_NULL_SHA,              SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_ECDH_ECDSA_WITH_NULL_SHA,            SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_NULL_SHA,                   SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_NULL_SHA256,                SSL_ALLOWED, PR_FALSE, PR_FALSE},
 { TLS_RSA_WITH_NULL_MD5,                   SSL_ALLOWED, PR_FALSE, PR_FALSE},
};
/* clang-format on */

/* This is the default supported set of signature schemes.  The order of the
 * hashes here is all that is important, since that will (sometimes) determine
 * which hash we use.  The key pair (i.e., cert) is the primary thing that
 * determines what we use and this doesn't affect how we select key pairs.  The
 * order of signature types is based on the same rules for ordering we use for
 * cipher suites just for consistency.
 */
static const SSLSignatureScheme defaultSignatureSchemes[] = {
    ssl_sig_ecdsa_secp256r1_sha256,
    ssl_sig_ecdsa_secp384r1_sha384,
    ssl_sig_ecdsa_secp521r1_sha512,
    ssl_sig_ecdsa_sha1,
    ssl_sig_rsa_pss_rsae_sha256,
    ssl_sig_rsa_pss_rsae_sha384,
    ssl_sig_rsa_pss_rsae_sha512,
    ssl_sig_rsa_pkcs1_sha256,
    ssl_sig_rsa_pkcs1_sha384,
    ssl_sig_rsa_pkcs1_sha512,
    ssl_sig_rsa_pkcs1_sha1,
    ssl_sig_dsa_sha256,
    ssl_sig_dsa_sha384,
    ssl_sig_dsa_sha512,
    ssl_sig_dsa_sha1
};
PR_STATIC_ASSERT(PR_ARRAY_SIZE(defaultSignatureSchemes) <=
                 MAX_SIGNATURE_SCHEMES);

/* Verify that SSL_ImplementedCiphers and cipherSuites are in consistent order.
 */
#ifdef DEBUG
void
ssl3_CheckCipherSuiteOrderConsistency()
{
    unsigned int i;

    PORT_Assert(SSL_NumImplementedCiphers == PR_ARRAY_SIZE(cipherSuites));

    for (i = 0; i < PR_ARRAY_SIZE(cipherSuites); ++i) {
        PORT_Assert(SSL_ImplementedCiphers[i] == cipherSuites[i].cipher_suite);
    }
}
#endif

static const /*SSL3ClientCertificateType */ PRUint8 certificate_types[] = {
    ct_RSA_sign,
    ct_ECDSA_sign,
    ct_DSS_sign,
};

static SSL3Statistics ssl3stats;

static const ssl3KEADef kea_defs[] = {
    /* indexed by SSL3KeyExchangeAlgorithm */
    /* kea            exchKeyType signKeyType authKeyType ephemeral  oid */
    { kea_null, ssl_kea_null, nullKey, ssl_auth_null, PR_FALSE, 0 },
    { kea_rsa, ssl_kea_rsa, nullKey, ssl_auth_rsa_decrypt, PR_FALSE, SEC_OID_TLS_RSA },
    { kea_dh_dss, ssl_kea_dh, dsaKey, ssl_auth_dsa, PR_FALSE, SEC_OID_TLS_DH_DSS },
    { kea_dh_rsa, ssl_kea_dh, rsaKey, ssl_auth_rsa_sign, PR_FALSE, SEC_OID_TLS_DH_RSA },
    { kea_dhe_dss, ssl_kea_dh, dsaKey, ssl_auth_dsa, PR_TRUE, SEC_OID_TLS_DHE_DSS },
    { kea_dhe_rsa, ssl_kea_dh, rsaKey, ssl_auth_rsa_sign, PR_TRUE, SEC_OID_TLS_DHE_RSA },
    { kea_dh_anon, ssl_kea_dh, nullKey, ssl_auth_null, PR_TRUE, SEC_OID_TLS_DH_ANON },
    { kea_ecdh_ecdsa, ssl_kea_ecdh, nullKey, ssl_auth_ecdh_ecdsa, PR_FALSE, SEC_OID_TLS_ECDH_ECDSA },
    { kea_ecdhe_ecdsa, ssl_kea_ecdh, ecKey, ssl_auth_ecdsa, PR_TRUE, SEC_OID_TLS_ECDHE_ECDSA },
    { kea_ecdh_rsa, ssl_kea_ecdh, nullKey, ssl_auth_ecdh_rsa, PR_FALSE, SEC_OID_TLS_ECDH_RSA },
    { kea_ecdhe_rsa, ssl_kea_ecdh, rsaKey, ssl_auth_rsa_sign, PR_TRUE, SEC_OID_TLS_ECDHE_RSA },
    { kea_ecdh_anon, ssl_kea_ecdh, nullKey, ssl_auth_null, PR_TRUE, SEC_OID_TLS_ECDH_ANON },
    { kea_ecdhe_psk, ssl_kea_ecdh_psk, nullKey, ssl_auth_psk, PR_TRUE, SEC_OID_TLS_ECDHE_PSK },
    { kea_dhe_psk, ssl_kea_dh_psk, nullKey, ssl_auth_psk, PR_TRUE, SEC_OID_TLS_DHE_PSK },
    { kea_tls13_any, ssl_kea_tls13_any, nullKey, ssl_auth_tls13_any, PR_TRUE, SEC_OID_TLS13_KEA_ANY },
};

/* must use ssl_LookupCipherSuiteDef to access */
static const ssl3CipherSuiteDef cipher_suite_defs[] = {
    /*  cipher_suite                    bulk_cipher_alg mac_alg key_exchange_alg prf_hash */
    /*  Note that the prf_hash_alg is the hash function used by the PRF, see sslimpl.h.  */

    { TLS_NULL_WITH_NULL_NULL, cipher_null, ssl_mac_null, kea_null, ssl_hash_none },
    { TLS_RSA_WITH_NULL_MD5, cipher_null, ssl_mac_md5, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_NULL_SHA, cipher_null, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_NULL_SHA256, cipher_null, ssl_hmac_sha256, kea_rsa, ssl_hash_sha256 },
    { TLS_RSA_WITH_RC4_128_MD5, cipher_rc4, ssl_mac_md5, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_RC4_128_SHA, cipher_rc4, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_DES_CBC_SHA, cipher_des, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_3DES_EDE_CBC_SHA, cipher_3des, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_DHE_DSS_WITH_DES_CBC_SHA, cipher_des, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA,
      cipher_3des, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_DSS_WITH_RC4_128_SHA, cipher_rc4, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_RSA_WITH_DES_CBC_SHA, cipher_des, ssl_mac_sha, kea_dhe_rsa, ssl_hash_none },
    { TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,
      cipher_3des, ssl_mac_sha, kea_dhe_rsa, ssl_hash_none },

    /* New TLS cipher suites */
    { TLS_RSA_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_AES_128_CBC_SHA256, cipher_aes_128, ssl_hmac_sha256, kea_rsa, ssl_hash_sha256 },
    { TLS_DHE_DSS_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_RSA_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_dhe_rsa, ssl_hash_none },
    { TLS_DHE_RSA_WITH_AES_128_CBC_SHA256, cipher_aes_128, ssl_hmac_sha256, kea_dhe_rsa, ssl_hash_sha256 },
    { TLS_RSA_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_RSA_WITH_AES_256_CBC_SHA256, cipher_aes_256, ssl_hmac_sha256, kea_rsa, ssl_hash_sha256 },
    { TLS_DHE_DSS_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_RSA_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_dhe_rsa, ssl_hash_none },
    { TLS_DHE_RSA_WITH_AES_256_CBC_SHA256, cipher_aes_256, ssl_hmac_sha256, kea_dhe_rsa, ssl_hash_sha256 },
    { TLS_DHE_RSA_WITH_AES_256_GCM_SHA384, cipher_aes_256_gcm, ssl_mac_aead, kea_dhe_rsa, ssl_hash_sha384 },

    { TLS_RSA_WITH_SEED_CBC_SHA, cipher_seed, ssl_mac_sha, kea_rsa, ssl_hash_none },

    { TLS_RSA_WITH_CAMELLIA_128_CBC_SHA, cipher_camellia_128, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA,
      cipher_camellia_128, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA,
      cipher_camellia_128, ssl_mac_sha, kea_dhe_rsa, ssl_hash_none },
    { TLS_RSA_WITH_CAMELLIA_256_CBC_SHA, cipher_camellia_256, ssl_mac_sha, kea_rsa, ssl_hash_none },
    { TLS_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA,
      cipher_camellia_256, ssl_mac_sha, kea_dhe_dss, ssl_hash_none },
    { TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA,
      cipher_camellia_256, ssl_mac_sha, kea_dhe_rsa, ssl_hash_none },

    { TLS_DHE_RSA_WITH_AES_128_GCM_SHA256, cipher_aes_128_gcm, ssl_mac_aead, kea_dhe_rsa, ssl_hash_sha256 },
    { TLS_RSA_WITH_AES_128_GCM_SHA256, cipher_aes_128_gcm, ssl_mac_aead, kea_rsa, ssl_hash_sha256 },

    { TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, cipher_aes_128_gcm, ssl_mac_aead, kea_ecdhe_rsa, ssl_hash_sha256 },
    { TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, cipher_aes_128_gcm, ssl_mac_aead, kea_ecdhe_ecdsa, ssl_hash_sha256 },
    { TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, cipher_aes_256_gcm, ssl_mac_aead, kea_ecdhe_ecdsa, ssl_hash_sha384 },
    { TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, cipher_aes_256_gcm, ssl_mac_aead, kea_ecdhe_rsa, ssl_hash_sha384 },
    { TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384, cipher_aes_256, ssl_hmac_sha384, kea_ecdhe_ecdsa, ssl_hash_sha384 },
    { TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384, cipher_aes_256, ssl_hmac_sha384, kea_ecdhe_rsa, ssl_hash_sha384 },
    { TLS_DHE_DSS_WITH_AES_128_GCM_SHA256, cipher_aes_128_gcm, ssl_mac_aead, kea_dhe_dss, ssl_hash_sha256 },
    { TLS_DHE_DSS_WITH_AES_128_CBC_SHA256, cipher_aes_128, ssl_hmac_sha256, kea_dhe_dss, ssl_hash_sha256 },
    { TLS_DHE_DSS_WITH_AES_256_CBC_SHA256, cipher_aes_256, ssl_hmac_sha256, kea_dhe_dss, ssl_hash_sha256 },
    { TLS_DHE_DSS_WITH_AES_256_GCM_SHA384, cipher_aes_256_gcm, ssl_mac_aead, kea_dhe_dss, ssl_hash_sha384 },
    { TLS_RSA_WITH_AES_256_GCM_SHA384, cipher_aes_256_gcm, ssl_mac_aead, kea_rsa, ssl_hash_sha384 },

    { TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256, cipher_chacha20, ssl_mac_aead, kea_dhe_rsa, ssl_hash_sha256 },

    { TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256, cipher_chacha20, ssl_mac_aead, kea_ecdhe_rsa, ssl_hash_sha256 },
    { TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, cipher_chacha20, ssl_mac_aead, kea_ecdhe_ecdsa, ssl_hash_sha256 },

    { TLS_ECDH_ECDSA_WITH_NULL_SHA, cipher_null, ssl_mac_sha, kea_ecdh_ecdsa, ssl_hash_none },
    { TLS_ECDH_ECDSA_WITH_RC4_128_SHA, cipher_rc4, ssl_mac_sha, kea_ecdh_ecdsa, ssl_hash_none },
    { TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA, cipher_3des, ssl_mac_sha, kea_ecdh_ecdsa, ssl_hash_none },
    { TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_ecdh_ecdsa, ssl_hash_none },
    { TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_ecdh_ecdsa, ssl_hash_none },

    { TLS_ECDHE_ECDSA_WITH_NULL_SHA, cipher_null, ssl_mac_sha, kea_ecdhe_ecdsa, ssl_hash_none },
    { TLS_ECDHE_ECDSA_WITH_RC4_128_SHA, cipher_rc4, ssl_mac_sha, kea_ecdhe_ecdsa, ssl_hash_none },
    { TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA, cipher_3des, ssl_mac_sha, kea_ecdhe_ecdsa, ssl_hash_none },
    { TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_ecdhe_ecdsa, ssl_hash_none },
    { TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256, cipher_aes_128, ssl_hmac_sha256, kea_ecdhe_ecdsa, ssl_hash_sha256 },
    { TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_ecdhe_ecdsa, ssl_hash_none },

    { TLS_ECDH_RSA_WITH_NULL_SHA, cipher_null, ssl_mac_sha, kea_ecdh_rsa, ssl_hash_none },
    { TLS_ECDH_RSA_WITH_RC4_128_SHA, cipher_rc4, ssl_mac_sha, kea_ecdh_rsa, ssl_hash_none },
    { TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA, cipher_3des, ssl_mac_sha, kea_ecdh_rsa, ssl_hash_none },
    { TLS_ECDH_RSA_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_ecdh_rsa, ssl_hash_none },
    { TLS_ECDH_RSA_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_ecdh_rsa, ssl_hash_none },

    { TLS_ECDHE_RSA_WITH_NULL_SHA, cipher_null, ssl_mac_sha, kea_ecdhe_rsa, ssl_hash_none },
    { TLS_ECDHE_RSA_WITH_RC4_128_SHA, cipher_rc4, ssl_mac_sha, kea_ecdhe_rsa, ssl_hash_none },
    { TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA, cipher_3des, ssl_mac_sha, kea_ecdhe_rsa, ssl_hash_none },
    { TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA, cipher_aes_128, ssl_mac_sha, kea_ecdhe_rsa, ssl_hash_none },
    { TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256, cipher_aes_128, ssl_hmac_sha256, kea_ecdhe_rsa, ssl_hash_sha256 },
    { TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA, cipher_aes_256, ssl_mac_sha, kea_ecdhe_rsa, ssl_hash_none },

    { TLS_AES_128_GCM_SHA256, cipher_aes_128_gcm, ssl_mac_aead, kea_tls13_any, ssl_hash_sha256 },
    { TLS_CHACHA20_POLY1305_SHA256, cipher_chacha20, ssl_mac_aead, kea_tls13_any, ssl_hash_sha256 },
    { TLS_AES_256_GCM_SHA384, cipher_aes_256_gcm, ssl_mac_aead, kea_tls13_any, ssl_hash_sha384 },
};

static const CK_MECHANISM_TYPE auth_alg_defs[] = {
    CKM_INVALID_MECHANISM, /* ssl_auth_null */
    CKM_RSA_PKCS,          /* ssl_auth_rsa_decrypt */
    CKM_DSA, /* ? _SHA1 */ /* ssl_auth_dsa */
    CKM_INVALID_MECHANISM, /* ssl_auth_kea (unused) */
    CKM_ECDSA,             /* ssl_auth_ecdsa */
    CKM_ECDH1_DERIVE,      /* ssl_auth_ecdh_rsa */
    CKM_ECDH1_DERIVE,      /* ssl_auth_ecdh_ecdsa */
    CKM_RSA_PKCS,          /* ssl_auth_rsa_sign */
    CKM_RSA_PKCS_PSS,      /* ssl_auth_rsa_pss */
    CKM_NSS_HKDF_SHA256,   /* ssl_auth_psk (just check for HKDF) */
    CKM_INVALID_MECHANISM  /* ssl_auth_tls13_any */
};
PR_STATIC_ASSERT(PR_ARRAY_SIZE(auth_alg_defs) == ssl_auth_size);

static const CK_MECHANISM_TYPE kea_alg_defs[] = {
    CKM_INVALID_MECHANISM, /* ssl_kea_null */
    CKM_RSA_PKCS,          /* ssl_kea_rsa */
    CKM_DH_PKCS_DERIVE,    /* ssl_kea_dh */
    CKM_INVALID_MECHANISM, /* ssl_kea_fortezza (unused) */
    CKM_ECDH1_DERIVE,      /* ssl_kea_ecdh */
    CKM_ECDH1_DERIVE,      /* ssl_kea_ecdh_psk */
    CKM_DH_PKCS_DERIVE,    /* ssl_kea_dh_psk */
    CKM_INVALID_MECHANISM, /* ssl_kea_tls13_any */
    CKM_INVALID_MECHANISM, /* ssl_kea_ecdh_hybrid */
    CKM_INVALID_MECHANISM, /* ssl_kea_ecdh_hybrid_psk */
};
PR_STATIC_ASSERT(PR_ARRAY_SIZE(kea_alg_defs) == ssl_kea_size);

typedef struct SSLCipher2MechStr {
    SSLCipherAlgorithm calg;
    CK_MECHANISM_TYPE cmech;
} SSLCipher2Mech;

/* indexed by type SSLCipherAlgorithm */
static const SSLCipher2Mech alg2Mech[] = {
    /* calg,          cmech  */
    { ssl_calg_null, CKM_INVALID_MECHANISM },
    { ssl_calg_rc4, CKM_RC4 },
    { ssl_calg_rc2, CKM_RC2_CBC },
    { ssl_calg_des, CKM_DES_CBC },
    { ssl_calg_3des, CKM_DES3_CBC },
    { ssl_calg_idea, CKM_IDEA_CBC },
    { ssl_calg_fortezza, CKM_SKIPJACK_CBC64 },
    { ssl_calg_aes, CKM_AES_CBC },
    { ssl_calg_camellia, CKM_CAMELLIA_CBC },
    { ssl_calg_seed, CKM_SEED_CBC },
    { ssl_calg_aes_gcm, CKM_AES_GCM },
    { ssl_calg_chacha20, CKM_CHACHA20_POLY1305 },
};

const PRUint8 tls12_downgrade_random[] = { 0x44, 0x4F, 0x57, 0x4E,
                                           0x47, 0x52, 0x44, 0x01 };
const PRUint8 tls1_downgrade_random[] = { 0x44, 0x4F, 0x57, 0x4E,
                                          0x47, 0x52, 0x44, 0x00 };
PR_STATIC_ASSERT(sizeof(tls12_downgrade_random) ==
                 sizeof(tls1_downgrade_random));

/* The ECCWrappedKeyInfo structure defines how various pieces of
 * information are laid out within wrappedSymmetricWrappingkey
 * for ECDH key exchange. Since wrappedSymmetricWrappingkey is
 * a 512-byte buffer (see sslimpl.h), the variable length field
 * in ECCWrappedKeyInfo can be at most (512 - 8) = 504 bytes.
 *
 * XXX For now, NSS only supports named elliptic curves of size 571 bits
 * or smaller. The public value will fit within 145 bytes and EC params
 * will fit within 12 bytes. We'll need to revisit this when NSS
 * supports arbitrary curves.
 */
#define MAX_EC_WRAPPED_KEY_BUFLEN 504

typedef struct ECCWrappedKeyInfoStr {
    PRUint16 size;                          /* EC public key size in bits */
    PRUint16 encodedParamLen;               /* length (in bytes) of DER encoded EC params */
    PRUint16 pubValueLen;                   /* length (in bytes) of EC public value */
    PRUint16 wrappedKeyLen;                 /* length (in bytes) of the wrapped key */
    PRUint8 var[MAX_EC_WRAPPED_KEY_BUFLEN]; /* this buffer contains the */
    /* EC public-key params, the EC public value and the wrapped key  */
} ECCWrappedKeyInfo;

CK_MECHANISM_TYPE
ssl3_Alg2Mech(SSLCipherAlgorithm calg)
{
    PORT_Assert(alg2Mech[calg].calg == calg);
    return alg2Mech[calg].cmech;
}

#if defined(TRACE)

static char *
ssl3_DecodeHandshakeType(int msgType)
{
    char *rv;
    static char line[40];

    switch (msgType) {
        case ssl_hs_hello_request:
            rv = "hello_request (0)";
            break;
        case ssl_hs_client_hello:
            rv = "client_hello  (1)";
            break;
        case ssl_hs_server_hello:
            rv = "server_hello  (2)";
            break;
        case ssl_hs_hello_verify_request:
            rv = "hello_verify_request (3)";
            break;
        case ssl_hs_new_session_ticket:
            rv = "new_session_ticket (4)";
            break;
        case ssl_hs_end_of_early_data:
            rv = "end_of_early_data (5)";
            break;
        case ssl_hs_hello_retry_request:
            rv = "hello_retry_request (6)";
            break;
        case ssl_hs_encrypted_extensions:
            rv = "encrypted_extensions (8)";
            break;
        case ssl_hs_certificate:
            rv = "certificate  (11)";
            break;
        case ssl_hs_server_key_exchange:
            rv = "server_key_exchange (12)";
            break;
        case ssl_hs_certificate_request:
            rv = "certificate_request (13)";
            break;
        case ssl_hs_server_hello_done:
            rv = "server_hello_done   (14)";
            break;
        case ssl_hs_certificate_verify:
            rv = "certificate_verify  (15)";
            break;
        case ssl_hs_client_key_exchange:
            rv = "client_key_exchange (16)";
            break;
        case ssl_hs_finished:
            rv = "finished     (20)";
            break;
        case ssl_hs_certificate_status:
            rv = "certificate_status  (22)";
            break;
        case ssl_hs_key_update:
            rv = "key_update   (24)";
            break;
        case ssl_hs_compressed_certificate:
            rv = "compressed certificate (25)";
            break;
        default:
            snprintf(line, sizeof(line), "*UNKNOWN* handshake type! (%d)", msgType);
            rv = line;
    }
    return rv;
}

static char *
ssl3_DecodeContentType(int msgType)
{
    char *rv;
    static char line[40];

    switch (msgType) {
        case ssl_ct_change_cipher_spec:
            rv = "change_cipher_spec (20)";
            break;
        case ssl_ct_alert:
            rv = "alert      (21)";
            break;
        case ssl_ct_handshake:
            rv = "handshake  (22)";
            break;
        case ssl_ct_application_data:
            rv = "application_data (23)";
            break;
        case ssl_ct_ack:
            rv = "ack (26)";
            break;
        default:
            snprintf(line, sizeof(line), "*UNKNOWN* record type! (%d)", msgType);
            rv = line;
    }
    return rv;
}

#endif

SSL3Statistics *
SSL_GetStatistics(void)
{
    return &ssl3stats;
}

typedef struct tooLongStr {
#if defined(IS_LITTLE_ENDIAN)
    PRInt32 low;
    PRInt32 high;
#else
    PRInt32 high;
    PRInt32 low;
#endif
} tooLong;

void
SSL_AtomicIncrementLong(long *x)
{
    if ((sizeof *x) == sizeof(PRInt32)) {
        PR_ATOMIC_INCREMENT((PRInt32 *)x);
    } else {
        tooLong *tl = (tooLong *)x;
        if (PR_ATOMIC_INCREMENT(&tl->low) == 0)
            PR_ATOMIC_INCREMENT(&tl->high);
    }
}

PRBool
ssl3_CipherSuiteAllowedForVersionRange(ssl3CipherSuite cipherSuite,
                                       const SSLVersionRange *vrange)
{
    switch (cipherSuite) {
        case TLS_DHE_RSA_WITH_AES_256_CBC_SHA256:
        case TLS_RSA_WITH_AES_256_CBC_SHA256:
        case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256:
        case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384:
        case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256:
        case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384:
        case TLS_DHE_RSA_WITH_AES_128_CBC_SHA256:
        case TLS_RSA_WITH_AES_128_CBC_SHA256:
        case TLS_RSA_WITH_AES_128_GCM_SHA256:
        case TLS_RSA_WITH_AES_256_GCM_SHA384:
        case TLS_DHE_DSS_WITH_AES_128_CBC_SHA256:
        case TLS_DHE_DSS_WITH_AES_256_CBC_SHA256:
        case TLS_RSA_WITH_NULL_SHA256:
        case TLS_DHE_DSS_WITH_AES_128_GCM_SHA256:
        case TLS_DHE_DSS_WITH_AES_256_GCM_SHA384:
        case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
        case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
        case TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
        case TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
        case TLS_DHE_RSA_WITH_AES_128_GCM_SHA256:
        case TLS_DHE_RSA_WITH_AES_256_GCM_SHA384:
        case TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256:
        case TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256:
        case TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256:
            return vrange->max >= SSL_LIBRARY_VERSION_TLS_1_2 &&
                   vrange->min < SSL_LIBRARY_VERSION_TLS_1_3;

        /* RFC 4492: ECC cipher suites need TLS extensions to negotiate curves and
         * point formats.*/
        case TLS_ECDH_ECDSA_WITH_NULL_SHA:
        case TLS_ECDH_ECDSA_WITH_RC4_128_SHA:
        case TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA:
        case TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA:
        case TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA:
        case TLS_ECDHE_ECDSA_WITH_NULL_SHA:
        case TLS_ECDHE_ECDSA_WITH_RC4_128_SHA:
        case TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA:
        case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA:
        case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
        case TLS_ECDH_RSA_WITH_NULL_SHA:
        case TLS_ECDH_RSA_WITH_RC4_128_SHA:
        case TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA:
        case TLS_ECDH_RSA_WITH_AES_128_CBC_SHA:
        case TLS_ECDH_RSA_WITH_AES_256_CBC_SHA:
        case TLS_ECDHE_RSA_WITH_NULL_SHA:
        case TLS_ECDHE_RSA_WITH_RC4_128_SHA:
        case TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA:
        case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA:
        case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
            return vrange->max >= SSL_LIBRARY_VERSION_TLS_1_0 &&
                   vrange->min < SSL_LIBRARY_VERSION_TLS_1_3;

        case TLS_AES_128_GCM_SHA256:
        case TLS_AES_256_GCM_SHA384:
        case TLS_CHACHA20_POLY1305_SHA256:
            return vrange->max >= SSL_LIBRARY_VERSION_TLS_1_3;

        default:
            return vrange->min < SSL_LIBRARY_VERSION_TLS_1_3;
    }
}

/* return pointer to ssl3CipherSuiteDef for suite, or NULL */
/* XXX This does a linear search.  A binary search would be better. */
const ssl3CipherSuiteDef *
ssl_LookupCipherSuiteDef(ssl3CipherSuite suite)
{
    int cipher_suite_def_len =
        sizeof(cipher_suite_defs) / sizeof(cipher_suite_defs[0]);
    int i;

    for (i = 0; i < cipher_suite_def_len; i++) {
        if (cipher_suite_defs[i].cipher_suite == suite)
            return &cipher_suite_defs[i];
    }
    PORT_Assert(PR_FALSE); /* We should never get here. */
    PORT_SetError(SSL_ERROR_UNKNOWN_CIPHER_SUITE);
    return NULL;
}

/* Find the cipher configuration struct associate with suite */
/* XXX This does a linear search.  A binary search would be better. */
static ssl3CipherSuiteCfg *
ssl_LookupCipherSuiteCfgMutable(ssl3CipherSuite suite,
                                ssl3CipherSuiteCfg *suites)
{
    int i;

    for (i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        if (suites[i].cipher_suite == suite)
            return &suites[i];
    }
    /* return NULL and let the caller handle it.  */
    PORT_SetError(SSL_ERROR_UNKNOWN_CIPHER_SUITE);
    return NULL;
}

const ssl3CipherSuiteCfg *
ssl_LookupCipherSuiteCfg(ssl3CipherSuite suite, const ssl3CipherSuiteCfg *suites)
{
    return ssl_LookupCipherSuiteCfgMutable(suite,
                                           CONST_CAST(ssl3CipherSuiteCfg, suites));
}

static PRBool
ssl_NamedGroupTypeEnabled(const sslSocket *ss, SSLKEAType keaType)
{
    unsigned int i;
    for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
        if (ss->namedGroupPreferences[i] &&
            ss->namedGroupPreferences[i]->keaType == keaType) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

static PRBool
ssl_KEAEnabled(const sslSocket *ss, SSLKEAType keaType)
{
    switch (keaType) {
        case ssl_kea_rsa:
            return PR_TRUE;

        case ssl_kea_dh:
        case ssl_kea_dh_psk: {
            if (ss->sec.isServer && !ss->opt.enableServerDhe) {
                return PR_FALSE;
            }

            if (ss->sec.isServer) {
                /* If the server requires named FFDHE groups, then the client
                 * must have included an FFDHE group. peerSupportsFfdheGroups
                 * is set to true in ssl_HandleSupportedGroupsXtn(). */
                if (ss->opt.requireDHENamedGroups &&
                    !ss->xtnData.peerSupportsFfdheGroups) {
                    return PR_FALSE;
                }

                /* We can use the weak DH group if all of these are true:
                 * 1. We don't require named groups.
                 * 2. The peer doesn't support named groups.
                 * 3. This isn't TLS 1.3.
                 * 4. The weak group is enabled. */
                if (!ss->opt.requireDHENamedGroups &&
                    !ss->xtnData.peerSupportsFfdheGroups &&
                    ss->version < SSL_LIBRARY_VERSION_TLS_1_3 &&
                    ss->ssl3.dheWeakGroupEnabled) {
                    return PR_TRUE;
                }
            } else {
                if (ss->vrange.min < SSL_LIBRARY_VERSION_TLS_1_3 &&
                    !ss->opt.requireDHENamedGroups) {
                    /* The client enables DHE cipher suites even if no DHE groups
                     * are enabled. Only if this isn't TLS 1.3 and named groups
                     * are not required. */
                    return PR_TRUE;
                }
            }
            return ssl_NamedGroupTypeEnabled(ss, ssl_kea_dh);
        }

        case ssl_kea_ecdh:
        case ssl_kea_ecdh_psk:
            return ssl_NamedGroupTypeEnabled(ss, ssl_kea_ecdh);

        case ssl_kea_ecdh_hybrid:
        case ssl_kea_ecdh_hybrid_psk:
            if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
                return PR_FALSE;
            }
            return ssl_NamedGroupTypeEnabled(ss, ssl_kea_ecdh_hybrid);

        case ssl_kea_tls13_any:
            return PR_TRUE;

        case ssl_kea_fortezza:
        default:
            PORT_Assert(0);
    }
    return PR_FALSE;
}

static PRBool
ssl_HasCert(const sslSocket *ss, PRUint16 maxVersion, SSLAuthType authType)
{
    PRCList *cursor;
    if (authType == ssl_auth_null || authType == ssl_auth_psk || authType == ssl_auth_tls13_any) {
        return PR_TRUE;
    }
    for (cursor = PR_NEXT_LINK(&ss->serverCerts);
         cursor != &ss->serverCerts;
         cursor = PR_NEXT_LINK(cursor)) {
        sslServerCert *cert = (sslServerCert *)cursor;
        if (!cert->serverKeyPair ||
            !cert->serverKeyPair->privKey ||
            !cert->serverCertChain ||
            !SSL_CERT_IS(cert, authType)) {
            continue;
        }
        /* When called from ssl3_config_match_init(), all the EC curves will be
         * enabled, so this will essentially do nothing (unless we implement
         * curve configuration).  However, once we have seen the
         * supported_groups extension and this is called from config_match(),
         * this will filter out certificates with an unsupported curve.
         *
         * If we might negotiate TLS 1.3, skip this test as group configuration
         * doesn't affect choices in TLS 1.3.
         */
        if (maxVersion < SSL_LIBRARY_VERSION_TLS_1_3 &&
            (authType == ssl_auth_ecdsa ||
             authType == ssl_auth_ecdh_ecdsa ||
             authType == ssl_auth_ecdh_rsa) &&
            !ssl_NamedGroupEnabled(ss, cert->namedCurve)) {
            continue;
        }
        return PR_TRUE;
    }
    if (authType == ssl_auth_rsa_sign) {
        return ssl_HasCert(ss, maxVersion, ssl_auth_rsa_pss);
    }
    return PR_FALSE;
}

/* return true if the scheme is allowed by policy, This prevents
 * failures later when our actual signatures are rejected by
 * policy by either ssl code, or lower level NSS code */
static PRBool
ssl_SchemePolicyOK(SSLSignatureScheme scheme, PRUint32 require)
{
    /* Hash policy. */
    PRUint32 policy;
    SECOidTag hashOID = ssl3_HashTypeToOID(ssl_SignatureSchemeToHashType(scheme));
    SECOidTag sigOID;

    /* policy bits needed to enable a SignatureScheme */
    SECStatus rv = NSS_GetAlgorithmPolicy(hashOID, &policy);
    if (rv == SECSuccess &&
        (policy & require) != require) {
        return PR_FALSE;
    }

    /* ssl_SignatureSchemeToAuthType reports rsa for rsa_pss_rsae, but we
     * actually implement pss signatures when we sign, so just use RSA_PSS
     * for all RSA PSS Siganture schemes */
    if (ssl_IsRsaPssSignatureScheme(scheme)) {
        sigOID = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
    } else {
        sigOID = ssl3_AuthTypeToOID(ssl_SignatureSchemeToAuthType(scheme));
    }
    /* Signature Policy. */
    rv = NSS_GetAlgorithmPolicy(sigOID, &policy);
    if (rv == SECSuccess &&
        (policy & require) != require) {
        return PR_FALSE;
    }
    return PR_TRUE;
}

/* Check that a signature scheme is accepted.
 * Both by policy and by having a token that supports it. */
static PRBool
ssl_SignatureSchemeAccepted(PRUint16 minVersion,
                            SSLSignatureScheme scheme,
                            PRBool forCert)
{
    /* Disable RSA-PSS schemes if there are no tokens to verify them. */
    if (ssl_IsRsaPssSignatureScheme(scheme)) {
        if (!PK11_TokenExists(auth_alg_defs[ssl_auth_rsa_pss])) {
            return PR_FALSE;
        }
    } else if (!forCert && ssl_IsRsaPkcs1SignatureScheme(scheme)) {
        /* Disable PKCS#1 signatures if we are limited to TLS 1.3.
         * We still need to advertise PKCS#1 signatures in CH and CR
         * for certificate signatures.
         */
        if (minVersion >= SSL_LIBRARY_VERSION_TLS_1_3) {
            return PR_FALSE;
        }
    } else if (ssl_IsDsaSignatureScheme(scheme)) {
        /* DSA: not in TLS 1.3, and check policy. */
        if (minVersion >= SSL_LIBRARY_VERSION_TLS_1_3) {
            return PR_FALSE;
        }
    }

    return ssl_SchemePolicyOK(scheme, kSSLSigSchemePolicy);
}

static SECStatus
ssl_CheckSignatureSchemes(sslSocket *ss)
{
    if (ss->vrange.max < SSL_LIBRARY_VERSION_TLS_1_2) {
        return SECSuccess;
    }

    /* If this is a server using TLS 1.3, we just need to have one signature
     * scheme for which we have a usable certificate.
     *
     * Note: Certificates for earlier TLS versions are checked along with the
     * cipher suite in ssl3_config_match_init. */
    if (ss->sec.isServer && ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3) {
        PRBool foundCert = PR_FALSE;
        for (unsigned int i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
            SSLAuthType authType =
                ssl_SignatureSchemeToAuthType(ss->ssl3.signatureSchemes[i]);
            if (ssl_HasCert(ss, ss->vrange.max, authType)) {
                foundCert = PR_TRUE;
                break;
            }
        }
        if (!foundCert) {
            PORT_SetError(SSL_ERROR_NO_SUPPORTED_SIGNATURE_ALGORITHM);
            return SECFailure;
        }
    }

    /* Ensure that there is a signature scheme that can be accepted.*/
    for (unsigned int i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
        if (ssl_SignatureSchemeAccepted(ss->vrange.min,
                                        ss->ssl3.signatureSchemes[i],
                                        PR_FALSE /* forCert */)) {
            return SECSuccess;
        }
    }
    PORT_SetError(SSL_ERROR_NO_SUPPORTED_SIGNATURE_ALGORITHM);
    return SECFailure;
}

/* For a server, check that a signature scheme that can be used with the
 * provided authType is both enabled and usable. */
static PRBool
ssl_HasSignatureScheme(const sslSocket *ss, SSLAuthType authType)
{
    PORT_Assert(ss->sec.isServer);
    PORT_Assert(ss->ssl3.hs.preliminaryInfo & ssl_preinfo_version);
    PORT_Assert(authType != ssl_auth_null);
    PORT_Assert(authType != ssl_auth_tls13_any);
    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_2 ||
        authType == ssl_auth_rsa_decrypt ||
        authType == ssl_auth_ecdh_rsa ||
        authType == ssl_auth_ecdh_ecdsa) {
        return PR_TRUE;
    }
    for (unsigned int i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
        SSLSignatureScheme scheme = ss->ssl3.signatureSchemes[i];
        SSLAuthType schemeAuthType = ssl_SignatureSchemeToAuthType(scheme);
        PRBool acceptable = authType == schemeAuthType ||
                            (schemeAuthType == ssl_auth_rsa_pss &&
                             authType == ssl_auth_rsa_sign);
        if (acceptable && ssl_SignatureSchemeAccepted(ss->version, scheme, PR_FALSE /* forCert */)) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

/* Initialize the suite->isPresent value for config_match
 * Returns count of enabled ciphers supported by extant tokens,
 * regardless of policy or user preference.
 * If this returns zero, the user cannot do SSL v3.
 */
unsigned int
ssl3_config_match_init(sslSocket *ss)
{
    ssl3CipherSuiteCfg *suite;
    const ssl3CipherSuiteDef *cipher_def;
    SSLCipherAlgorithm cipher_alg;
    CK_MECHANISM_TYPE cipher_mech;
    SSLAuthType authType;
    SSLKEAType keaType;
    unsigned int i;
    unsigned int numPresent = 0;
    unsigned int numEnabled = 0;

    PORT_Assert(ss);
    if (!ss) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return 0;
    }
    if (SSL_ALL_VERSIONS_DISABLED(&ss->vrange)) {
        return 0;
    }
    if (ss->sec.isServer && ss->psk &&
        PR_CLIST_IS_EMPTY(&ss->serverCerts) &&
        (ss->opt.requestCertificate || ss->opt.requireCertificate)) {
        /* PSK and certificate auth cannot be combined. */
        PORT_SetError(SSL_ERROR_NO_CERTIFICATE);
        return 0;
    }
    if (ssl_CheckSignatureSchemes(ss) != SECSuccess) {
        return 0; /* Code already set. */
    }

    ssl_FilterSupportedGroups(ss);
    for (i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        suite = &ss->cipherSuites[i];
        if (suite->enabled) {
            ++numEnabled;
            /* We need the cipher defs to see if we have a token that can handle
             * this cipher.  It isn't part of the static definition.
             */
            cipher_def = ssl_LookupCipherSuiteDef(suite->cipher_suite);
            if (!cipher_def) {
                suite->isPresent = PR_FALSE;
                continue;
            }
            cipher_alg = ssl_GetBulkCipherDef(cipher_def)->calg;
            cipher_mech = ssl3_Alg2Mech(cipher_alg);

            /* Mark the suites that are backed by real tokens, certs and keys */
            suite->isPresent = PR_TRUE;

            authType = kea_defs[cipher_def->key_exchange_alg].authKeyType;
            if (authType != ssl_auth_null && authType != ssl_auth_tls13_any) {
                if (ss->sec.isServer &&
                    !(ssl_HasCert(ss, ss->vrange.max, authType) &&
                      ssl_HasSignatureScheme(ss, authType))) {
                    suite->isPresent = PR_FALSE;
                } else if (!PK11_TokenExists(auth_alg_defs[authType])) {
                    suite->isPresent = PR_FALSE;
                }
            }

            keaType = kea_defs[cipher_def->key_exchange_alg].exchKeyType;
            if (keaType != ssl_kea_null &&
                keaType != ssl_kea_tls13_any &&
                !PK11_TokenExists(kea_alg_defs[keaType])) {
                suite->isPresent = PR_FALSE;
            }

            if (cipher_alg != ssl_calg_null &&
                !PK11_TokenExists(cipher_mech)) {
                suite->isPresent = PR_FALSE;
            }

            if (suite->isPresent) {
                ++numPresent;
            }
        }
    }
    PORT_AssertArg(numPresent > 0 || numEnabled == 0);
    if (numPresent == 0) {
        PORT_SetError(SSL_ERROR_NO_CIPHERS_SUPPORTED);
    }
    return numPresent;
}

/* Return PR_TRUE if suite is usable.  This if the suite is permitted by policy,
 * enabled, has a certificate (as needed), has a viable key agreement method, is
 * usable with the negotiated TLS version, and is otherwise usable. */
PRBool
ssl3_config_match(const ssl3CipherSuiteCfg *suite, PRUint8 policy,
                  const SSLVersionRange *vrange, const sslSocket *ss)
{
    const ssl3CipherSuiteDef *cipher_def;
    const ssl3KEADef *kea_def;

    if (!suite) {
        PORT_Assert(suite);
        return PR_FALSE;
    }

    PORT_Assert(policy != SSL_NOT_ALLOWED);
    if (policy == SSL_NOT_ALLOWED)
        return PR_FALSE;

    if (!suite->enabled || !suite->isPresent)
        return PR_FALSE;

    if ((suite->policy == SSL_NOT_ALLOWED) ||
        (suite->policy > policy))
        return PR_FALSE;

    PORT_Assert(ss != NULL);
    cipher_def = ssl_LookupCipherSuiteDef(suite->cipher_suite);
    PORT_Assert(cipher_def != NULL);
    kea_def = &kea_defs[cipher_def->key_exchange_alg];
    PORT_Assert(kea_def != NULL);
    if (!ssl_KEAEnabled(ss, kea_def->exchKeyType)) {
        return PR_FALSE;
    }

    if (ss->sec.isServer && !ssl_HasCert(ss, vrange->max, kea_def->authKeyType)) {
        return PR_FALSE;
    }

    /* If a PSK is selected, disable suites that use a different hash than
     * the PSK. We advertise non-PSK-compatible suites in the CH, as we could
     * fallback to certificate auth. The client handler will check hash
     * compatibility before committing to use the PSK. */
    if (ss->xtnData.selectedPsk) {
        if (ss->xtnData.selectedPsk->hash != cipher_def->prf_hash) {
            return PR_FALSE;
        }
    }

    return ssl3_CipherSuiteAllowedForVersionRange(suite->cipher_suite, vrange);
}

/* For TLS 1.3, when resuming, check for a ciphersuite that is both compatible
 * with the identified ciphersuite and enabled. */
static PRBool
tls13_ResumptionCompatible(sslSocket *ss, ssl3CipherSuite suite)
{
    SSLVersionRange vrange = { SSL_LIBRARY_VERSION_TLS_1_3,
                               SSL_LIBRARY_VERSION_TLS_1_3 };
    SSLHashType hash = tls13_GetHashForCipherSuite(suite);
    for (unsigned int i = 0; i < PR_ARRAY_SIZE(cipher_suite_defs); i++) {
        if (cipher_suite_defs[i].prf_hash == hash) {
            const ssl3CipherSuiteCfg *suiteCfg =
                ssl_LookupCipherSuiteCfg(cipher_suite_defs[i].cipher_suite,
                                         ss->cipherSuites);
            if (suite && ssl3_config_match(suiteCfg, ss->ssl3.policy, &vrange, ss)) {
                return PR_TRUE;
            }
        }
    }
    return PR_FALSE;
}

/*
 * Null compression, mac and encryption functions
 */
SECStatus
Null_Cipher(void *ctx, unsigned char *output, unsigned int *outputLen, unsigned int maxOutputLen,
            const unsigned char *input, unsigned int inputLen)
{
    if (inputLen > maxOutputLen) {
        *outputLen = 0; /* Match PK11_CipherOp in setting outputLen */
        PORT_SetError(SEC_ERROR_OUTPUT_LEN);
        return SECFailure;
    }
    *outputLen = inputLen;
    if (inputLen > 0 && input != output) {
        PORT_Memcpy(output, input, inputLen);
    }
    return SECSuccess;
}

/* Wrapper around PK11_CipherOp to avoid undefined behavior due to incompatible
 * function pointer type cast
 */
static SECStatus
SSLCipher_PK11_CipherOp(void *ctx, unsigned char *output, unsigned int *outputLen, unsigned int maxOutputLen,
                        const unsigned char *input, unsigned int inputLen)
{
    PK11Context *pctx = ctx;
    PORT_Assert(maxOutputLen <= INT_MAX);
    int signedOutputLen = maxOutputLen;
    SECStatus rv = PK11_CipherOp(pctx, output, &signedOutputLen, maxOutputLen, input, inputLen);
    PORT_Assert(signedOutputLen >= 0);
    *outputLen = signedOutputLen;
    return rv;
}

/*
 * SSL3 Utility functions
 */

static void
ssl_SetSpecVersions(sslSocket *ss, ssl3CipherSpec *spec)
{
    spec->version = ss->version;
    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        tls13_SetSpecRecordVersion(ss, spec);
    } else if (IS_DTLS(ss)) {
        spec->recordVersion = dtls_TLSVersionToDTLSVersion(ss->version);
    } else {
        spec->recordVersion = ss->version;
    }
}

/* allowLargerPeerVersion controls whether the function will select the
 * highest enabled SSL version or fail when peerVersion is greater than the
 * highest enabled version.
 *
 * If allowLargerPeerVersion is true, peerVersion is the peer's highest
 * enabled version rather than the peer's selected version.
 */
SECStatus
ssl3_NegotiateVersion(sslSocket *ss, SSL3ProtocolVersion peerVersion,
                      PRBool allowLargerPeerVersion)
{
    SSL3ProtocolVersion negotiated;

    /* Prevent negotiating to a lower version in response to a TLS 1.3 HRR. */
    if (ss->ssl3.hs.helloRetry) {
        PORT_SetError(SSL_ERROR_UNSUPPORTED_VERSION);
        return SECFailure;
    }

    if (SSL_ALL_VERSIONS_DISABLED(&ss->vrange)) {
        PORT_SetError(SSL_ERROR_SSL_DISABLED);
        return SECFailure;
    }

    if (peerVersion < ss->vrange.min ||
        (peerVersion > ss->vrange.max && !allowLargerPeerVersion)) {
        PORT_SetError(SSL_ERROR_UNSUPPORTED_VERSION);
        return SECFailure;
    }

    negotiated = PR_MIN(peerVersion, ss->vrange.max);
    PORT_Assert(ssl3_VersionIsSupported(ss->protocolVariant, negotiated));
    if (ss->firstHsDone && ss->version != negotiated) {
        PORT_SetError(SSL_ERROR_UNSUPPORTED_VERSION);
        return SECFailure;
    }

    ss->version = negotiated;
    return SECSuccess;
}

/* Used by the client when the server produces a version number.
 * This reads, validates, and normalizes the value. */
SECStatus
ssl_ClientReadVersion(sslSocket *ss, PRUint8 **b, unsigned int *len,
                      SSL3ProtocolVersion *version)
{
    SSL3ProtocolVersion v;
    PRUint32 temp;
    SECStatus rv;

    rv = ssl3_ConsumeHandshakeNumber(ss, &temp, 2, b, len);
    if (rv != SECSuccess) {
        return SECFailure; /* alert has been sent */
    }
    v = (SSL3ProtocolVersion)temp;

    if (IS_DTLS(ss)) {
        v = dtls_DTLSVersionToTLSVersion(v);
        /* Check for failure. */
        if (!v || v > SSL_LIBRARY_VERSION_MAX_SUPPORTED) {
            SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
            return SECFailure;
        }
    }

    /* You can't negotiate TLS 1.3 this way. */
    if (v >= SSL_LIBRARY_VERSION_TLS_1_3) {
        SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
        return SECFailure;
    }
    *version = v;
    return SECSuccess;
}

SECStatus
ssl3_GetNewRandom(SSL3Random random)
{
    SECStatus rv;

    rv = PK11_GenerateRandom(random, SSL3_RANDOM_LENGTH);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_GENERATE_RANDOM_FAILURE);
    }
    return rv;
}

SECStatus
ssl3_SignHashesWithPrivKey(SSL3Hashes *hash, SECKEYPrivateKey *key,
                           SSLSignatureScheme scheme, PRBool isTls, SECItem *buf)
{
    SECStatus rv = SECFailure;
    PRBool doDerEncode = PR_FALSE;
    PRBool useRsaPss = ssl_IsRsaPssSignatureScheme(scheme);
    SECItem hashItem;

    buf->data = NULL;

    switch (SECKEY_GetPrivateKeyType(key)) {
        case rsaKey:
            hashItem.data = hash->u.raw;
            hashItem.len = hash->len;
            break;
        case dsaKey:
            doDerEncode = isTls;
            /* ssl_hash_none is used to specify the MD5/SHA1 concatenated hash.
             * In that case, we use just the SHA1 part. */
            if (hash->hashAlg == ssl_hash_none) {
                hashItem.data = hash->u.s.sha;
                hashItem.len = sizeof(hash->u.s.sha);
            } else {
                hashItem.data = hash->u.raw;
                hashItem.len = hash->len;
            }
            break;
        case ecKey:
            doDerEncode = PR_TRUE;
            /* ssl_hash_none is used to specify the MD5/SHA1 concatenated hash.
             * In that case, we use just the SHA1 part. */
            if (hash->hashAlg == ssl_hash_none) {
                hashItem.data = hash->u.s.sha;
                hashItem.len = sizeof(hash->u.s.sha);
            } else {
                hashItem.data = hash->u.raw;
                hashItem.len = hash->len;
            }
            break;
        default:
            PORT_SetError(SEC_ERROR_INVALID_KEY);
            goto done;
    }
    PRINT_BUF(60, (NULL, "hash(es) to be signed", hashItem.data, hashItem.len));

    if (useRsaPss || hash->hashAlg == ssl_hash_none) {
        CK_MECHANISM_TYPE mech = PK11_MapSignKeyType(key->keyType);
        int signatureLen = PK11_SignatureLen(key);
        PRInt32 optval;

        SECItem *params = NULL;
        CK_RSA_PKCS_PSS_PARAMS pssParams;
        SECItem pssParamsItem = { siBuffer,
                                  (unsigned char *)&pssParams,
                                  sizeof(pssParams) };

        if (signatureLen <= 0) {
            PORT_SetError(SEC_ERROR_INVALID_KEY);
            goto done;
        }
        /* since we are calling PK11_SignWithMechanism directly, we need to check the
         * key policy ourselves (which is already checked in SGN_Digest */
        rv = NSS_OptionGet(NSS_KEY_SIZE_POLICY_FLAGS, &optval);
        if ((rv == SECSuccess) &&
            ((optval & NSS_KEY_SIZE_POLICY_SIGN_FLAG) == NSS_KEY_SIZE_POLICY_SIGN_FLAG)) {
            rv = SECKEY_EnforceKeySize(key->keyType, SECKEY_PrivateKeyStrengthInBits(key),
                                       SEC_ERROR_SIGNATURE_ALGORITHM_DISABLED);
            if (rv != SECSuccess) {
                goto done; /* error code already set */
            }
        }

        buf->len = (unsigned)signatureLen;
        buf->data = (unsigned char *)PORT_Alloc(signatureLen);
        if (!buf->data)
            goto done; /* error code was set. */

        if (useRsaPss) {
            pssParams.hashAlg = ssl3_GetHashMechanismByHashType(hash->hashAlg);
            pssParams.mgf = ssl3_GetMgfMechanismByHashType(hash->hashAlg);
            pssParams.sLen = hashItem.len;
            params = &pssParamsItem;
            mech = CKM_RSA_PKCS_PSS;
        }

        rv = PK11_SignWithMechanism(key, mech, params, buf, &hashItem);
    } else {
        SECOidTag hashOID = ssl3_HashTypeToOID(hash->hashAlg);
        rv = SGN_Digest(key, hashOID, buf, &hashItem);
    }
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_SIGN_HASHES_FAILURE);
    } else if (doDerEncode) {
        SECItem derSig = { siBuffer, NULL, 0 };

        /* This also works for an ECDSA signature */
        rv = DSAU_EncodeDerSigWithLen(&derSig, buf, buf->len);
        if (rv == SECSuccess) {
            PORT_Free(buf->data); /* discard unencoded signature. */
            *buf = derSig;        /* give caller encoded signature. */
        } else if (derSig.data) {
            PORT_Free(derSig.data);
        }
    }

    PRINT_BUF(60, (NULL, "signed hashes", (unsigned char *)buf->data, buf->len));
done:
    if (rv != SECSuccess && buf->data) {
        PORT_Free(buf->data);
        buf->data = NULL;
    }
    return rv;
}

/* Called by ssl3_SendServerKeyExchange and ssl3_SendCertificateVerify */
SECStatus
ssl3_SignHashes(sslSocket *ss, SSL3Hashes *hash, SECKEYPrivateKey *key,
                SECItem *buf)
{
    SECStatus rv = SECFailure;
    PRBool isTLS = (PRBool)(ss->version > SSL_LIBRARY_VERSION_3_0);
    SSLSignatureScheme scheme = ss->ssl3.hs.signatureScheme;

    rv = ssl3_SignHashesWithPrivKey(hash, key, scheme, isTLS, buf);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->sec.isServer) {
        ss->sec.signatureScheme = scheme;
        ss->sec.authType = ssl_SignatureSchemeToAuthType(scheme);
    }

    return SECSuccess;
}

/* Called from ssl3_VerifySignedHashes and tls13_HandleCertificateVerify. */
SECStatus
ssl_VerifySignedHashesWithPubKey(sslSocket *ss, SECKEYPublicKey *key,
                                 SSLSignatureScheme scheme,
                                 SSL3Hashes *hash, SECItem *buf)
{
    SECItem *signature = NULL;
    SECStatus rv = SECFailure;
    SECItem hashItem;
    SECOidTag encAlg;
    SECOidTag hashAlg;
    void *pwArg = ss->pkcs11PinArg;
    PRBool isRsaPssScheme = ssl_IsRsaPssSignatureScheme(scheme);

    PRINT_BUF(60, (NULL, "check signed hashes", buf->data, buf->len));

    hashAlg = ssl3_HashTypeToOID(hash->hashAlg);
    switch (SECKEY_GetPublicKeyType(key)) {
        case rsaKey:
            encAlg = SEC_OID_PKCS1_RSA_ENCRYPTION;
            hashItem.data = hash->u.raw;
            hashItem.len = hash->len;
            if (scheme == ssl_sig_none) {
                scheme = ssl_sig_rsa_pkcs1_sha1md5;
            }
            break;
        case dsaKey:
            encAlg = SEC_OID_ANSIX9_DSA_SIGNATURE;
            /* ssl_hash_none is used to specify the MD5/SHA1 concatenated hash.
             * In that case, we use just the SHA1 part. */
            if (hash->hashAlg == ssl_hash_none) {
                hashItem.data = hash->u.s.sha;
                hashItem.len = sizeof(hash->u.s.sha);
            } else {
                hashItem.data = hash->u.raw;
                hashItem.len = hash->len;
            }
            /* Allow DER encoded DSA signatures in SSL 3.0 */
            if (ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0 ||
                buf->len != SECKEY_SignatureLen(key)) {
                signature = DSAU_DecodeDerSigToLen(buf, SECKEY_SignatureLen(key));
                if (!signature) {
                    PORT_SetError(SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE);
                    goto loser;
                }
                buf = signature;
            }
            if (scheme == ssl_sig_none) {
                scheme = ssl_sig_dsa_sha1;
            }
            break;

        case ecKey:
            encAlg = SEC_OID_ANSIX962_EC_PUBLIC_KEY;
            /* ssl_hash_none is used to specify the MD5/SHA1 concatenated hash.
             * In that case, we use just the SHA1 part.
             * ECDSA signatures always encode the integers r and s using ASN.1
             * (unlike DSA where ASN.1 encoding is used with TLS but not with
             * SSL3). So we can use VFY_VerifyDigestDirect for ECDSA.
             */
            if (hash->hashAlg == ssl_hash_none) {
                hashAlg = SEC_OID_SHA1;
                hashItem.data = hash->u.s.sha;
                hashItem.len = sizeof(hash->u.s.sha);
            } else {
                hashItem.data = hash->u.raw;
                hashItem.len = hash->len;
            }
            if (scheme == ssl_sig_none) {
                scheme = ssl_sig_ecdsa_sha1;
            }
            break;

        default:
            PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
            goto loser;
    }

    PRINT_BUF(60, (NULL, "hash(es) to be verified",
                   hashItem.data, hashItem.len));

    if (isRsaPssScheme ||
        hashAlg == SEC_OID_UNKNOWN ||
        SECKEY_GetPublicKeyType(key) == dsaKey) {
        /* VFY_VerifyDigestDirect requires DSA signatures to be DER-encoded.
         * DSA signatures are DER-encoded in TLS but not in SSL3 and the code
         * above always removes the DER encoding of DSA signatures when
         * present. Thus DSA signatures are always verified with PK11_Verify.
         */
        CK_MECHANISM_TYPE mech = PK11_MapSignKeyType(key->keyType);

        SECItem *params = NULL;
        CK_RSA_PKCS_PSS_PARAMS pssParams;
        SECItem pssParamsItem = { siBuffer,
                                  (unsigned char *)&pssParams,
                                  sizeof(pssParams) };

        if (isRsaPssScheme) {
            pssParams.hashAlg = ssl3_GetHashMechanismByHashType(hash->hashAlg);
            pssParams.mgf = ssl3_GetMgfMechanismByHashType(hash->hashAlg);
            pssParams.sLen = hashItem.len;
            params = &pssParamsItem;
            mech = CKM_RSA_PKCS_PSS;
        }

        rv = PK11_VerifyWithMechanism(key, mech, params, buf, &hashItem, pwArg);
    } else {
        rv = VFY_VerifyDigestDirect(&hashItem, key, buf, encAlg, hashAlg,
                                    pwArg);
    }
    if (signature) {
        SECITEM_FreeItem(signature, PR_TRUE);
    }
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE);
    }
    if (!ss->sec.isServer) {
        ss->sec.signatureScheme = scheme;
        ss->sec.authType = ssl_SignatureSchemeToAuthType(scheme);
    }

loser:
#ifdef UNSAFE_FUZZER_MODE
    rv = SECSuccess;
    PORT_SetError(0);
#endif
    return rv;
}

/* Called from ssl3_HandleServerKeyExchange, ssl3_HandleCertificateVerify */
SECStatus
ssl3_VerifySignedHashes(sslSocket *ss, SSLSignatureScheme scheme, SSL3Hashes *hash,
                        SECItem *buf)
{
    SECKEYPublicKey *pubKey =
        SECKEY_ExtractPublicKey(&ss->sec.peerCert->subjectPublicKeyInfo);
    if (pubKey == NULL) {
        ssl_MapLowLevelError(SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE);
        return SECFailure;
    }
    SECStatus rv = ssl_VerifySignedHashesWithPubKey(ss, pubKey, scheme,
                                                    hash, buf);
    SECKEY_DestroyPublicKey(pubKey);
    return rv;
}

/* Caller must set hiLevel error code. */
/* Called from ssl3_ComputeDHKeyHash
 * which are called from ssl3_HandleServerKeyExchange.
 *
 * hashAlg: ssl_hash_none indicates the pre-1.2, MD5/SHA1 combination hash.
 */
SECStatus
ssl3_ComputeCommonKeyHash(SSLHashType hashAlg,
                          PRUint8 *hashBuf, unsigned int bufLen,
                          SSL3Hashes *hashes)
{
    SECStatus rv;
    SECOidTag hashOID;
    PRUint32 policy;

    if (hashAlg == ssl_hash_none) {
        if ((NSS_GetAlgorithmPolicy(SEC_OID_SHA1, &policy) == SECSuccess) &&
            !(policy & NSS_USE_ALG_IN_SSL_KX)) {
            ssl_MapLowLevelError(SSL_ERROR_UNSUPPORTED_HASH_ALGORITHM);
            return SECFailure;
        }
        rv = PK11_HashBuf(SEC_OID_MD5, hashes->u.s.md5, hashBuf, bufLen);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
            return rv;
        }
        rv = PK11_HashBuf(SEC_OID_SHA1, hashes->u.s.sha, hashBuf, bufLen);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            return rv;
        }
        hashes->len = MD5_LENGTH + SHA1_LENGTH;
    } else {
        hashOID = ssl3_HashTypeToOID(hashAlg);
        if ((NSS_GetAlgorithmPolicy(hashOID, &policy) == SECSuccess) &&
            !(policy & NSS_USE_ALG_IN_SSL_KX)) {
            ssl_MapLowLevelError(SSL_ERROR_UNSUPPORTED_HASH_ALGORITHM);
            return SECFailure;
        }
        hashes->len = HASH_ResultLenByOidTag(hashOID);
        if (hashes->len == 0 || hashes->len > sizeof(hashes->u.raw)) {
            ssl_MapLowLevelError(SSL_ERROR_UNSUPPORTED_HASH_ALGORITHM);
            return SECFailure;
        }
        rv = PK11_HashBuf(hashOID, hashes->u.raw, hashBuf, bufLen);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
            return rv;
        }
    }
    hashes->hashAlg = hashAlg;
    return SECSuccess;
}

/* Caller must set hiLevel error code. */
/* Called from ssl3_HandleServerKeyExchange. */
static SECStatus
ssl3_ComputeDHKeyHash(sslSocket *ss, SSLHashType hashAlg, SSL3Hashes *hashes,
                      SECItem dh_p, SECItem dh_g, SECItem dh_Ys, PRBool padY)
{
    sslBuffer buf = SSL_BUFFER_EMPTY;
    SECStatus rv;
    unsigned int yLen;
    unsigned int i;

    PORT_Assert(dh_p.data);
    PORT_Assert(dh_g.data);
    PORT_Assert(dh_Ys.data);

    rv = sslBuffer_Append(&buf, ss->ssl3.hs.client_random, SSL3_RANDOM_LENGTH);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = sslBuffer_Append(&buf, ss->ssl3.hs.server_random, SSL3_RANDOM_LENGTH);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* p */
    rv = sslBuffer_AppendVariable(&buf, dh_p.data, dh_p.len, 2);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* g */
    rv = sslBuffer_AppendVariable(&buf, dh_g.data, dh_g.len, 2);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* y - complicated by padding */
    yLen = padY ? dh_p.len : dh_Ys.len;
    rv = sslBuffer_AppendNumber(&buf, yLen, 2);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* If we're padding Y, dh_Ys can't be longer than dh_p. */
    PORT_Assert(!padY || dh_p.len >= dh_Ys.len);
    for (i = dh_Ys.len; i < yLen; ++i) {
        rv = sslBuffer_AppendNumber(&buf, 0, 1);
        if (rv != SECSuccess) {
            goto loser;
        }
    }
    rv = sslBuffer_Append(&buf, dh_Ys.data, dh_Ys.len);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = ssl3_ComputeCommonKeyHash(hashAlg, SSL_BUFFER_BASE(&buf),
                                   SSL_BUFFER_LEN(&buf), hashes);
    if (rv != SECSuccess) {
        goto loser;
    }

    PRINT_BUF(95, (NULL, "DHkey hash: ", SSL_BUFFER_BASE(&buf),
                   SSL_BUFFER_LEN(&buf)));
    if (hashAlg == ssl_hash_none) {
        PRINT_BUF(95, (NULL, "DHkey hash: MD5 result",
                       hashes->u.s.md5, MD5_LENGTH));
        PRINT_BUF(95, (NULL, "DHkey hash: SHA1 result",
                       hashes->u.s.sha, SHA1_LENGTH));
    } else {
        PRINT_BUF(95, (NULL, "DHkey hash: result",
                       hashes->u.raw, hashes->len));
    }

    sslBuffer_Clear(&buf);
    return SECSuccess;

loser:
    sslBuffer_Clear(&buf);
    return SECFailure;
}

static SECStatus
ssl3_SetupPendingCipherSpec(sslSocket *ss, SSLSecretDirection direction,
                            const ssl3CipherSuiteDef *suiteDef,
                            ssl3CipherSpec **specp)
{
    ssl3CipherSpec *spec;
    const ssl3CipherSpec *prev;

    prev = (direction == ssl_secret_write) ? ss->ssl3.cwSpec : ss->ssl3.crSpec;
    if (prev->epoch == PR_UINT16_MAX) {
        PORT_SetError(SSL_ERROR_RENEGOTIATION_NOT_ALLOWED);
        return SECFailure;
    }

    spec = ssl_CreateCipherSpec(ss, direction);
    if (!spec) {
        return SECFailure;
    }

    spec->cipherDef = ssl_GetBulkCipherDef(suiteDef);
    spec->macDef = ssl_GetMacDef(ss, suiteDef);

    spec->epoch = prev->epoch + 1;
    spec->nextSeqNum = 0;
    if (IS_DTLS(ss) && direction == ssl_secret_read) {
        dtls_InitRecvdRecords(&spec->recvdRecords);
    }
    ssl_SetSpecVersions(ss, spec);

    ssl_SaveCipherSpec(ss, spec);
    *specp = spec;
    return SECSuccess;
}

/* Fill in the pending cipher spec with info from the selected ciphersuite.
** This is as much initialization as we can do without having key material.
** Called from ssl3_HandleServerHello(), ssl3_SendServerHello()
** Caller must hold the ssl3 handshake lock.
** Acquires & releases SpecWriteLock.
*/
SECStatus
ssl3_SetupBothPendingCipherSpecs(sslSocket *ss)
{
    ssl3CipherSuite suite = ss->ssl3.hs.cipher_suite;
    SSL3KeyExchangeAlgorithm kea;
    const ssl3CipherSuiteDef *suiteDef;
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->version < SSL_LIBRARY_VERSION_TLS_1_3);

    ssl_GetSpecWriteLock(ss); /*******************************/

    /* This hack provides maximal interoperability with SSL 3 servers. */
    if (ss->ssl3.cwSpec->macDef->mac == ssl_mac_null) {
        /* SSL records are not being MACed. */
        ss->ssl3.cwSpec->version = ss->version;
    }

    SSL_TRC(3, ("%d: SSL3[%d]: Set XXX Pending Cipher Suite to 0x%04x",
                SSL_GETPID(), ss->fd, suite));

    suiteDef = ssl_LookupCipherSuiteDef(suite);
    if (suiteDef == NULL) {
        goto loser;
    }

    if (IS_DTLS(ss)) {
        /* Double-check that we did not pick an RC4 suite */
        PORT_Assert(suiteDef->bulk_cipher_alg != cipher_rc4);
    }

    ss->ssl3.hs.suite_def = suiteDef;

    kea = suiteDef->key_exchange_alg;
    ss->ssl3.hs.kea_def = &kea_defs[kea];
    PORT_Assert(ss->ssl3.hs.kea_def->kea == kea);

    rv = ssl3_SetupPendingCipherSpec(ss, ssl_secret_read, suiteDef,
                                     &ss->ssl3.prSpec);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = ssl3_SetupPendingCipherSpec(ss, ssl_secret_write, suiteDef,
                                     &ss->ssl3.pwSpec);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (ssl3_ExtensionNegotiated(ss, ssl_record_size_limit_xtn)) {
        ss->ssl3.prSpec->recordSizeLimit = PR_MIN(MAX_FRAGMENT_LENGTH,
                                                  ss->opt.recordSizeLimit);
        ss->ssl3.pwSpec->recordSizeLimit = PR_MIN(MAX_FRAGMENT_LENGTH,
                                                  ss->xtnData.recordSizeLimit);
    }

    ssl_ReleaseSpecWriteLock(ss); /*******************************/
    return SECSuccess;

loser:
    ssl_ReleaseSpecWriteLock(ss);
    return SECFailure;
}

/* ssl3_BuildRecordPseudoHeader writes the SSL/TLS pseudo-header (the data which
 * is included in the MAC or AEAD additional data) to |buf|. See
 * https://tools.ietf.org/html/rfc5246#section-6.2.3.3 for the definition of the
 * AEAD additional data.
 *
 * TLS pseudo-header includes the record's version field, SSL's doesn't. Which
 * pseudo-header definition to use should be decided based on the version of
 * the protocol that was negotiated when the cipher spec became current, NOT
 * based on the version value in the record itself, and the decision is passed
 * to this function as the |includesVersion| argument. But, the |version|
 * argument should be the record's version value.
 */
static SECStatus
ssl3_BuildRecordPseudoHeader(DTLSEpoch epoch,
                             sslSequenceNumber seqNum,
                             SSLContentType ct,
                             PRBool includesVersion,
                             SSL3ProtocolVersion version,
                             PRBool isDTLS,
                             int length,
                             sslBuffer *buf, SSL3ProtocolVersion v)
{
    SECStatus rv;
    if (isDTLS && v < SSL_LIBRARY_VERSION_TLS_1_3) {
        rv = sslBuffer_AppendNumber(buf, epoch, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        rv = sslBuffer_AppendNumber(buf, seqNum, 6);
    } else {
        rv = sslBuffer_AppendNumber(buf, seqNum, 8);
    }
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(buf, ct, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* SSL3 MAC doesn't include the record's version field. */
    if (includesVersion) {
        /* TLS MAC and AEAD additional data include version. */
        rv = sslBuffer_AppendNumber(buf, version, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    rv = sslBuffer_AppendNumber(buf, length, 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    return SECSuccess;
}

/* Initialize encryption and MAC contexts for pending spec.
 * Master Secret already is derived.
 * Caller holds Spec write lock.
 */
static SECStatus
ssl3_InitPendingContexts(sslSocket *ss, ssl3CipherSpec *spec)
{
    CK_MECHANISM_TYPE encMechanism;
    CK_ATTRIBUTE_TYPE encMode;
    SECItem macParam;
    CK_ULONG macLength;
    SECItem iv;
    SSLCipherAlgorithm calg;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSpecWriteLock(ss));

    calg = spec->cipherDef->calg;
    PORT_Assert(alg2Mech[calg].calg == calg);

    if (spec->cipherDef->type != type_aead) {
        macLength = spec->macDef->mac_size;

        /*
        ** Now setup the MAC contexts,
        **   crypto contexts are setup below.
        */
        macParam.data = (unsigned char *)&macLength;
        macParam.len = sizeof(macLength);
        macParam.type = siBuffer;

        spec->keyMaterial.macContext = PK11_CreateContextBySymKey(
            spec->macDef->mmech, CKA_SIGN, spec->keyMaterial.macKey, &macParam);
        if (!spec->keyMaterial.macContext) {
            ssl_MapLowLevelError(SSL_ERROR_SYM_KEY_CONTEXT_FAILURE);
            return SECFailure;
        }
    }

    /*
    ** Now setup the crypto contexts.
    */
    if (calg == ssl_calg_null) {
        spec->cipher = Null_Cipher;
        return SECSuccess;
    }

    encMechanism = ssl3_Alg2Mech(calg);
    encMode = (spec->direction == ssl_secret_write) ? CKA_ENCRYPT : CKA_DECRYPT;
    if (spec->cipherDef->type == type_aead) {
        encMode |= CKA_NSS_MESSAGE;
        iv.data = NULL;
        iv.len = 0;
    } else {
        spec->cipher = SSLCipher_PK11_CipherOp;
        iv.data = spec->keyMaterial.iv;
        iv.len = spec->cipherDef->iv_size;
    }

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

/* Complete the initialization of all keys, ciphers, MACs and their contexts
 * for the pending Cipher Spec.
 * Called from: ssl3_SendClientKeyExchange  (for Full handshake)
 *              ssl3_HandleRSAClientKeyExchange (for Full handshake)
 *              ssl3_HandleServerHello      (for session restart)
 *              ssl3_HandleClientHello      (for session restart)
 * Sets error code, but caller probably should override to disambiguate.
 *
 * If |secret| is a master secret from a previous connection is reused, |derive|
 * is PR_FALSE.  If the secret is a pre-master secret, then |derive| is PR_TRUE
 * and the master secret is derived from |secret|.
 */
SECStatus
ssl3_InitPendingCipherSpecs(sslSocket *ss, PK11SymKey *secret, PRBool derive)
{
    PK11SymKey *masterSecret;
    ssl3CipherSpec *pwSpec;
    ssl3CipherSpec *prSpec;
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(secret);

    ssl_GetSpecWriteLock(ss); /**************************************/

    PORT_Assert(ss->ssl3.pwSpec);
    PORT_Assert(ss->ssl3.cwSpec->epoch == ss->ssl3.crSpec->epoch);
    prSpec = ss->ssl3.prSpec;
    pwSpec = ss->ssl3.pwSpec;

    if (ss->ssl3.cwSpec->epoch == PR_UINT16_MAX) {
        /* The problem here is that we have rehandshaked too many
         * times (you are not allowed to wrap the epoch). The
         * spec says you should be discarding the connection
         * and start over, so not much we can do here. */
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        goto loser;
    }

    if (derive) {
        rv = ssl3_ComputeMasterSecret(ss, secret, &masterSecret);
        if (rv != SECSuccess) {
            goto loser;
        }
    } else {
        masterSecret = secret;
    }

    PORT_Assert(masterSecret);
    rv = ssl3_DeriveConnectionKeys(ss, masterSecret);
    if (rv != SECSuccess) {
        if (derive) {
            /* masterSecret was created here. */
            PK11_FreeSymKey(masterSecret);
        }
        goto loser;
    }

    /* Both cipher specs maintain a reference to the master secret, since each
     * is managed and freed independently. */
    prSpec->masterSecret = masterSecret;
    pwSpec->masterSecret = PK11_ReferenceSymKey(masterSecret);
    rv = ssl3_InitPendingContexts(ss, ss->ssl3.prSpec);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = ssl3_InitPendingContexts(ss, ss->ssl3.pwSpec);
    if (rv != SECSuccess) {
        goto loser;
    }

    ssl_ReleaseSpecWriteLock(ss); /******************************/
    return SECSuccess;

loser:
    ssl_ReleaseSpecWriteLock(ss); /******************************/
    ssl_MapLowLevelError(SSL_ERROR_SESSION_KEY_GEN_FAILURE);
    return SECFailure;
}

/*
 * 60 bytes is 3 times the maximum length MAC size that is supported.
 */
static const unsigned char mac_pad_1[60] = {
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36
};
static const unsigned char mac_pad_2[60] = {
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c
};

/* Called from: ssl3_SendRecord()
** Caller must already hold the SpecReadLock. (wish we could assert that!)
*/
static SECStatus
ssl3_ComputeRecordMAC(
    ssl3CipherSpec *spec,
    const unsigned char *header,
    unsigned int headerLen,
    const PRUint8 *input,
    int inputLen,
    unsigned char *outbuf,
    unsigned int *outLen)
{
    PK11Context *context;
    int macSize = spec->macDef->mac_size;
    SECStatus rv;

    PRINT_BUF(95, (NULL, "frag hash1: header", header, headerLen));
    PRINT_BUF(95, (NULL, "frag hash1: input", input, inputLen));

    if (spec->macDef->mac == ssl_mac_null) {
        *outLen = 0;
        return SECSuccess;
    }

    context = spec->keyMaterial.macContext;
    rv = PK11_DigestBegin(context);
    rv |= PK11_DigestOp(context, header, headerLen);
    rv |= PK11_DigestOp(context, input, inputLen);
    rv |= PK11_DigestFinal(context, outbuf, outLen, macSize);
    PORT_Assert(rv != SECSuccess || *outLen == (unsigned)macSize);

    PRINT_BUF(95, (NULL, "frag hash2: result", outbuf, *outLen));

    if (rv != SECSuccess) {
        rv = SECFailure;
        ssl_MapLowLevelError(SSL_ERROR_MAC_COMPUTATION_FAILURE);
    }
    return rv;
}

/* Called from: ssl3_HandleRecord()
 * Caller must already hold the SpecReadLock. (wish we could assert that!)
 *
 * On entry:
 *   originalLen >= inputLen >= MAC size
 */
static SECStatus
ssl3_ComputeRecordMACConstantTime(
    ssl3CipherSpec *spec,
    const unsigned char *header,
    unsigned int headerLen,
    const PRUint8 *input,
    int inputLen,
    int originalLen,
    unsigned char *outbuf,
    unsigned int *outLen)
{
    CK_MECHANISM_TYPE macType;
    CK_NSS_MAC_CONSTANT_TIME_PARAMS params;
    SECItem param, inputItem, outputItem;
    int macSize = spec->macDef->mac_size;
    SECStatus rv;

    PORT_Assert(inputLen >= spec->macDef->mac_size);
    PORT_Assert(originalLen >= inputLen);

    if (spec->macDef->mac == ssl_mac_null) {
        *outLen = 0;
        return SECSuccess;
    }

    macType = CKM_NSS_HMAC_CONSTANT_TIME;
    if (spec->version == SSL_LIBRARY_VERSION_3_0) {
        macType = CKM_NSS_SSL3_MAC_CONSTANT_TIME;
    }

    params.macAlg = spec->macDef->mmech;
    params.ulBodyTotalLen = originalLen;
    params.pHeader = (unsigned char *)header; /* const cast */
    params.ulHeaderLen = headerLen;

    param.data = (unsigned char *)&params;
    param.len = sizeof(params);
    param.type = 0;

    inputItem.data = (unsigned char *)input;
    inputItem.len = inputLen;
    inputItem.type = 0;

    outputItem.data = outbuf;
    outputItem.len = *outLen;
    outputItem.type = 0;

    rv = PK11_SignWithSymKey(spec->keyMaterial.macKey, macType, &param,
                             &outputItem, &inputItem);
    if (rv != SECSuccess) {
        if (PORT_GetError() == SEC_ERROR_INVALID_ALGORITHM) {
            /* ssl3_ComputeRecordMAC() expects the MAC to have been removed
             * from the input length already. */
            return ssl3_ComputeRecordMAC(spec, header, headerLen,
                                         input, inputLen - macSize,
                                         outbuf, outLen);
        }

        *outLen = 0;
        rv = SECFailure;
        ssl_MapLowLevelError(SSL_ERROR_MAC_COMPUTATION_FAILURE);
        return rv;
    }

    PORT_Assert(outputItem.len == (unsigned)macSize);
    *outLen = outputItem.len;

    return rv;
}

static PRBool
ssl3_ClientAuthTokenPresent(sslSessionID *sid)
{
    PK11SlotInfo *slot = NULL;
    PRBool isPresent = PR_TRUE;

    /* we only care if we are doing client auth */
    if (!sid || !sid->u.ssl3.clAuthValid) {
        return PR_TRUE;
    }

    /* get the slot */
    slot = SECMOD_LookupSlot(sid->u.ssl3.clAuthModuleID,
                             sid->u.ssl3.clAuthSlotID);
    if (slot == NULL ||
        !PK11_IsPresent(slot) ||
        sid->u.ssl3.clAuthSeries != PK11_GetSlotSeries(slot) ||
        sid->u.ssl3.clAuthSlotID != PK11_GetSlotID(slot) ||
        sid->u.ssl3.clAuthModuleID != PK11_GetModuleID(slot) ||
        (PK11_NeedLogin(slot) && !PK11_IsLoggedIn(slot, NULL))) {
        isPresent = PR_FALSE;
    }
    if (slot) {
        PK11_FreeSlot(slot);
    }
    return isPresent;
}

/* Caller must hold the spec read lock. */
SECStatus
ssl3_MACEncryptRecord(ssl3CipherSpec *cwSpec,
                      PRBool isServer,
                      PRBool isDTLS,
                      SSLContentType ct,
                      const PRUint8 *pIn,
                      PRUint32 contentLen,
                      sslBuffer *wrBuf)
{
    SECStatus rv;
    PRUint32 macLen = 0;
    PRUint32 fragLen;
    PRUint32 p1Len, p2Len, oddLen = 0;
    unsigned int ivLen = 0;
    unsigned char pseudoHeaderBuf[13];
    sslBuffer pseudoHeader = SSL_BUFFER(pseudoHeaderBuf);
    unsigned int len;

    if (cwSpec->cipherDef->type == type_block &&
        cwSpec->version >= SSL_LIBRARY_VERSION_TLS_1_1) {
        /* Prepend the per-record explicit IV using technique 2b from
         * RFC 4346 section 6.2.3.2: The IV is a cryptographically
         * strong random number XORed with the CBC residue from the previous
         * record.
         */
        ivLen = cwSpec->cipherDef->iv_size;
        if (ivLen > SSL_BUFFER_SPACE(wrBuf)) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        rv = PK11_GenerateRandom(SSL_BUFFER_NEXT(wrBuf), ivLen);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_GENERATE_RANDOM_FAILURE);
            return rv;
        }
        rv = cwSpec->cipher(cwSpec->cipherContext,
                            SSL_BUFFER_NEXT(wrBuf), /* output */
                            &len,                   /* outlen */
                            ivLen,                  /* max outlen */
                            SSL_BUFFER_NEXT(wrBuf), /* input */
                            ivLen);                 /* input len */
        if (rv != SECSuccess || len != ivLen) {
            PORT_SetError(SSL_ERROR_ENCRYPTION_FAILURE);
            return SECFailure;
        }

        rv = sslBuffer_Skip(wrBuf, len, NULL);
        PORT_Assert(rv == SECSuccess); /* Can't fail. */
    }
    rv = ssl3_BuildRecordPseudoHeader(
        cwSpec->epoch, cwSpec->nextSeqNum, ct,
        cwSpec->version >= SSL_LIBRARY_VERSION_TLS_1_0, cwSpec->recordVersion,
        isDTLS, contentLen, &pseudoHeader, cwSpec->version);
    PORT_Assert(rv == SECSuccess);
    if (cwSpec->cipherDef->type == type_aead) {
        const unsigned int nonceLen = cwSpec->cipherDef->explicit_nonce_size;
        const unsigned int tagLen = cwSpec->cipherDef->tag_size;
        unsigned int ivOffset = 0;
        CK_GENERATOR_FUNCTION gen;
        /* ivOut includes the iv and the nonce and is the internal iv/nonce
         * for the AEAD function. On Encrypt, this is an in/out parameter */
        unsigned char ivOut[MAX_IV_LENGTH];
        ivLen = cwSpec->cipherDef->iv_size;

        PORT_Assert((ivLen + nonceLen) <= MAX_IV_LENGTH);
        PORT_Assert((ivLen + nonceLen) >= sizeof(sslSequenceNumber));

        if (nonceLen + contentLen + tagLen > SSL_BUFFER_SPACE(wrBuf)) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }

        if (nonceLen == 0) {
            ivOffset = ivLen - sizeof(sslSequenceNumber);
            gen = CKG_GENERATE_COUNTER_XOR;
        } else {
            ivOffset = ivLen;
            gen = CKG_GENERATE_COUNTER;
        }
        ivOffset = tls13_SetupAeadIv(isDTLS, cwSpec->version, ivOut, cwSpec->keyMaterial.iv,
                                     ivOffset, ivLen, cwSpec->epoch);
        rv = tls13_AEAD(cwSpec->cipherContext,
                        PR_FALSE,
                        gen, ivOffset * BPB,                /* iv generator params */
                        ivOut,                              /* iv in  */
                        ivOut,                              /* iv out */
                        ivLen + nonceLen,                   /* full iv length */
                        NULL, 0,                            /* nonce is generated*/
                        SSL_BUFFER_BASE(&pseudoHeader),     /* aad */
                        SSL_BUFFER_LEN(&pseudoHeader),      /* aadlen */
                        SSL_BUFFER_NEXT(wrBuf) + nonceLen,  /* output  */
                        &len,                               /* out len */
                        SSL_BUFFER_SPACE(wrBuf) - nonceLen, /* max out */
                        tagLen,
                        pIn, contentLen); /* input   */
        if (rv != SECSuccess) {
            PORT_SetError(SSL_ERROR_ENCRYPTION_FAILURE);
            return SECFailure;
        }
        len += nonceLen; /* include the nonce at the beginning */
        /* copy out the generated iv if we are using explict nonces */
        if (nonceLen) {
            PORT_Memcpy(SSL_BUFFER_NEXT(wrBuf), ivOut + ivLen, nonceLen);
        }

        rv = sslBuffer_Skip(wrBuf, len, NULL);
        PORT_Assert(rv == SECSuccess); /* Can't fail. */
    } else {
        int blockSize = cwSpec->cipherDef->block_size;

        /*
         * Add the MAC
         */
        rv = ssl3_ComputeRecordMAC(cwSpec, SSL_BUFFER_BASE(&pseudoHeader),
                                   SSL_BUFFER_LEN(&pseudoHeader),
                                   pIn, contentLen,
                                   SSL_BUFFER_NEXT(wrBuf) + contentLen, &macLen);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_MAC_COMPUTATION_FAILURE);
            return SECFailure;
        }
        p1Len = contentLen;
        p2Len = macLen;
        fragLen = contentLen + macLen; /* needs to be encrypted */
        PORT_Assert(fragLen <= MAX_FRAGMENT_LENGTH + 1024);

        /*
         * Pad the text (if we're doing a block cipher)
         * then Encrypt it
         */
        if (cwSpec->cipherDef->type == type_block) {
            unsigned char *pBuf;
            int padding_length;
            int i;

            oddLen = contentLen % blockSize;
            /* Assume blockSize is a power of two */
            padding_length = blockSize - 1 - ((fragLen) & (blockSize - 1));
            fragLen += padding_length + 1;
            PORT_Assert((fragLen % blockSize) == 0);

            /* Pad according to TLS rules (also acceptable to SSL3). */
            pBuf = SSL_BUFFER_NEXT(wrBuf) + fragLen - 1;
            for (i = padding_length + 1; i > 0; --i) {
                *pBuf-- = padding_length;
            }
            /* now, if contentLen is not a multiple of block size, fix it */
            p2Len = fragLen - p1Len;
        }
        if (p1Len < 256) {
            oddLen = p1Len;
            p1Len = 0;
        } else {
            p1Len -= oddLen;
        }
        if (oddLen) {
            p2Len += oddLen;
            PORT_Assert((blockSize < 2) ||
                        (p2Len % blockSize) == 0);
            memmove(SSL_BUFFER_NEXT(wrBuf) + p1Len, pIn + p1Len, oddLen);
        }
        if (p1Len > 0) {
            unsigned int cipherBytesPart1 = 0;
            rv = cwSpec->cipher(cwSpec->cipherContext,
                                SSL_BUFFER_NEXT(wrBuf), /* output */
                                &cipherBytesPart1,      /* actual outlen */
                                p1Len,                  /* max outlen */
                                pIn,
                                p1Len); /* input, and inputlen */
            PORT_Assert(rv == SECSuccess && cipherBytesPart1 == p1Len);
            if (rv != SECSuccess || cipherBytesPart1 != p1Len) {
                PORT_SetError(SSL_ERROR_ENCRYPTION_FAILURE);
                return SECFailure;
            }
            rv = sslBuffer_Skip(wrBuf, p1Len, NULL);
            PORT_Assert(rv == SECSuccess);
        }
        if (p2Len > 0) {
            unsigned int cipherBytesPart2 = 0;
            rv = cwSpec->cipher(cwSpec->cipherContext,
                                SSL_BUFFER_NEXT(wrBuf),
                                &cipherBytesPart2, /* output and actual outLen */
                                p2Len,             /* max outlen */
                                SSL_BUFFER_NEXT(wrBuf),
                                p2Len); /* input and inputLen*/
            PORT_Assert(rv == SECSuccess && cipherBytesPart2 == p2Len);
            if (rv != SECSuccess || cipherBytesPart2 != p2Len) {
                PORT_SetError(SSL_ERROR_ENCRYPTION_FAILURE);
                return SECFailure;
            }
            rv = sslBuffer_Skip(wrBuf, p2Len, NULL);
            PORT_Assert(rv == SECSuccess);
        }
    }

    return SECSuccess;
}

/* Note: though this can report failure, it shouldn't. */
SECStatus
ssl_InsertRecordHeader(const sslSocket *ss, ssl3CipherSpec *cwSpec,
                       SSLContentType contentType, sslBuffer *wrBuf,
                       PRBool *needsLength)
{
    SECStatus rv;

#ifndef UNSAFE_FUZZER_MODE
    if (cwSpec->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
        cwSpec->epoch > TrafficKeyClearText) {
        if (IS_DTLS(ss)) {
            return dtls13_InsertCipherTextHeader(ss, cwSpec, wrBuf,
                                                 needsLength);
        }
        contentType = ssl_ct_application_data;
    }
#endif
    rv = sslBuffer_AppendNumber(wrBuf, contentType, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(wrBuf, cwSpec->recordVersion, 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (IS_DTLS(ss)) {
        rv = sslBuffer_AppendNumber(wrBuf, cwSpec->epoch, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        rv = sslBuffer_AppendNumber(wrBuf, cwSpec->nextSeqNum, 6);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    *needsLength = PR_TRUE;
    return SECSuccess;
}

SECStatus
ssl_ProtectRecord(sslSocket *ss, ssl3CipherSpec *cwSpec, SSLContentType ct,
                  const PRUint8 *pIn, PRUint32 contentLen, sslBuffer *wrBuf)
{
    PRBool needsLength;
    unsigned int lenOffset;
    SECStatus rv;

    PORT_Assert(cwSpec->direction == ssl_secret_write);
    PORT_Assert(SSL_BUFFER_LEN(wrBuf) == 0);
    PORT_Assert(cwSpec->cipherDef->max_records <= RECORD_SEQ_MAX);

    if (cwSpec->nextSeqNum >= cwSpec->cipherDef->max_records) {
        SSL_TRC(3, ("%d: SSL[-]: write sequence number at limit 0x%0llx",
                    SSL_GETPID(), cwSpec->nextSeqNum));
        PORT_SetError(SSL_ERROR_TOO_MANY_RECORDS);
        return SECFailure;
    }

    rv = ssl_InsertRecordHeader(ss, cwSpec, ct, wrBuf, &needsLength);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (needsLength) {
        rv = sslBuffer_Skip(wrBuf, 2, &lenOffset);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

#ifdef UNSAFE_FUZZER_MODE
    {
        unsigned int len;
        rv = Null_Cipher(NULL, SSL_BUFFER_NEXT(wrBuf), &len,
                         SSL_BUFFER_SPACE(wrBuf), pIn, contentLen);
        if (rv != SECSuccess) {
            return SECFailure; /* error was set */
        }
        rv = sslBuffer_Skip(wrBuf, len, NULL);
        PORT_Assert(rv == SECSuccess); /* Can't fail. */
    }
#else
    if (cwSpec->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        PRUint8 *cipherText = SSL_BUFFER_NEXT(wrBuf);
        unsigned int bufLen = SSL_BUFFER_LEN(wrBuf);
        rv = tls13_ProtectRecord(ss, cwSpec, ct, pIn, contentLen, wrBuf);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        if (IS_DTLS(ss)) {
            bufLen = SSL_BUFFER_LEN(wrBuf) - bufLen;
            rv = dtls13_MaskSequenceNumber(ss, cwSpec,
                                           SSL_BUFFER_BASE(wrBuf),
                                           cipherText, bufLen);
        }
    } else {
        rv = ssl3_MACEncryptRecord(cwSpec, ss->sec.isServer, IS_DTLS(ss), ct,
                                   pIn, contentLen, wrBuf);
    }
#endif
    if (rv != SECSuccess) {
        return SECFailure; /* error was set */
    }

    if (needsLength) {
        /* Insert the length. */
        rv = sslBuffer_InsertLength(wrBuf, lenOffset, 2);
        if (rv != SECSuccess) {
            PORT_Assert(0); /* Can't fail. */
            return SECFailure;
        }
    }

    ++cwSpec->nextSeqNum;
    return SECSuccess;
}

SECStatus
ssl_ProtectNextRecord(sslSocket *ss, ssl3CipherSpec *spec, SSLContentType ct,
                      const PRUint8 *pIn, unsigned int nIn,
                      unsigned int *written)
{
    sslBuffer *wrBuf = &ss->sec.writeBuf;
    unsigned int contentLen;
    unsigned int spaceNeeded;
    SECStatus rv;

    contentLen = PR_MIN(nIn, spec->recordSizeLimit);
    spaceNeeded = contentLen + SSL3_BUFFER_FUDGE;
    if (spec->version >= SSL_LIBRARY_VERSION_TLS_1_1 &&
        spec->cipherDef->type == type_block) {
        spaceNeeded += spec->cipherDef->iv_size;
    }
    if (spaceNeeded > SSL_BUFFER_SPACE(wrBuf)) {
        rv = sslBuffer_Grow(wrBuf, spaceNeeded);
        if (rv != SECSuccess) {
            SSL_DBG(("%d: SSL3[%d]: failed to expand write buffer to %d",
                     SSL_GETPID(), ss->fd, spaceNeeded));
            return SECFailure;
        }
    }

    rv = ssl_ProtectRecord(ss, spec, ct, pIn, contentLen, wrBuf);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    PRINT_BUF(50, (ss, "send (encrypted) record data:",
                   SSL_BUFFER_BASE(wrBuf), SSL_BUFFER_LEN(wrBuf)));
    *written = contentLen;
    return SECSuccess;
}

/* Process the plain text before sending it.
 * Returns the number of bytes of plaintext that were successfully sent
 *  plus the number of bytes of plaintext that were copied into the
 *  output (write) buffer.
 * Returns -1 on an error.  PR_WOULD_BLOCK_ERROR is set if the error is blocking
 *  and not terminal.
 *
 * Notes on the use of the private ssl flags:
 * (no private SSL flags)
 *    Attempt to make and send SSL records for all plaintext
 *    If non-blocking and a send gets WOULD_BLOCK,
 *    or if the pending (ciphertext) buffer is not empty,
 *    then buffer remaining bytes of ciphertext into pending buf,
 *    and continue to do that for all succssive records until all
 *    bytes are used.
 * ssl_SEND_FLAG_FORCE_INTO_BUFFER
 *    As above, except this suppresses all write attempts, and forces
 *    all ciphertext into the pending ciphertext buffer.
 * ssl_SEND_FLAG_USE_EPOCH (for DTLS)
 *    Forces the use of the provided epoch
 */
PRInt32
ssl3_SendRecord(sslSocket *ss,
                ssl3CipherSpec *cwSpec, /* non-NULL for DTLS retransmits */
                SSLContentType ct,
                const PRUint8 *pIn, /* input buffer */
                PRInt32 nIn,        /* bytes of input */
                PRInt32 flags)
{
    sslBuffer *wrBuf = &ss->sec.writeBuf;
    ssl3CipherSpec *spec;
    SECStatus rv;
    PRInt32 totalSent = 0;

    SSL_TRC(3, ("%d: SSL3[%d] SendRecord type: %s nIn=%d",
                SSL_GETPID(), ss->fd, ssl3_DecodeContentType(ct),
                nIn));
    PRINT_BUF(50, (ss, "Send record (plain text)", pIn, nIn));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(SSL_BUFFER_LEN(wrBuf) == 0);

    if (ss->ssl3.fatalAlertSent) {
        SSL_TRC(3, ("%d: SSL3[%d] Suppress write, fatal alert already sent",
                    SSL_GETPID(), ss->fd));
        if (ct != ssl_ct_alert) {
            /* If we are sending an alert, then we already have an
             * error, so don't overwrite. */
            PORT_SetError(SSL_ERROR_HANDSHAKE_FAILED);
        }
        return -1;
    }

    /* check for Token Presence */
    if (!ssl3_ClientAuthTokenPresent(ss->sec.ci.sid)) {
        PORT_SetError(SSL_ERROR_TOKEN_INSERTION_REMOVAL);
        return -1;
    }

    if (ss->recordWriteCallback) {
        PRUint16 epoch;
        ssl_GetSpecReadLock(ss);
        epoch = ss->ssl3.cwSpec->epoch;
        ssl_ReleaseSpecReadLock(ss);
        rv = ss->recordWriteCallback(ss->fd, epoch, ct, pIn, nIn,
                                     ss->recordWriteCallbackArg);
        if (rv != SECSuccess) {
            return -1;
        }
        return nIn;
    }

    if (cwSpec) {
        /* cwSpec can only be set for retransmissions of the DTLS handshake. */
        PORT_Assert(IS_DTLS(ss) &&
                    (ct == ssl_ct_handshake ||
                     ct == ssl_ct_change_cipher_spec));
        spec = cwSpec;
    } else {
        spec = ss->ssl3.cwSpec;
    }

    while (nIn > 0) {
        unsigned int written = 0;
        PRInt32 sent;

        ssl_GetSpecReadLock(ss);
        rv = ssl_ProtectNextRecord(ss, spec, ct, pIn, nIn, &written);
        ssl_ReleaseSpecReadLock(ss);
        if (rv != SECSuccess) {
            goto loser;
        }

        PORT_Assert(written > 0);
        /* DTLS should not fragment non-application data here. */
        if (IS_DTLS(ss) && ct != ssl_ct_application_data) {
            PORT_Assert(written == nIn);
        }

        pIn += written;
        nIn -= written;
        PORT_Assert(nIn >= 0);

        /* If there's still some previously saved ciphertext,
         * or the caller doesn't want us to send the data yet,
         * then add all our new ciphertext to the amount previously saved.
         */
        if ((ss->pendingBuf.len > 0) ||
            (flags & ssl_SEND_FLAG_FORCE_INTO_BUFFER)) {

            rv = ssl_SaveWriteData(ss, SSL_BUFFER_BASE(wrBuf),
                                   SSL_BUFFER_LEN(wrBuf));
            if (rv != SECSuccess) {
                /* presumably a memory error, SEC_ERROR_NO_MEMORY */
                goto loser;
            }

            if (!(flags & ssl_SEND_FLAG_FORCE_INTO_BUFFER)) {
                ss->handshakeBegun = 1;
                sent = ssl_SendSavedWriteData(ss);
                if (sent < 0 && PR_GetError() != PR_WOULD_BLOCK_ERROR) {
                    ssl_MapLowLevelError(SSL_ERROR_SOCKET_WRITE_FAILURE);
                    goto loser;
                }
                if (ss->pendingBuf.len) {
                    flags |= ssl_SEND_FLAG_FORCE_INTO_BUFFER;
                }
            }
        } else {
            PORT_Assert(SSL_BUFFER_LEN(wrBuf) > 0);
            ss->handshakeBegun = 1;
            sent = ssl_DefSend(ss, SSL_BUFFER_BASE(wrBuf),
                               SSL_BUFFER_LEN(wrBuf),
                               flags & ~ssl_SEND_FLAG_MASK);
            if (sent < 0) {
                if (PORT_GetError() != PR_WOULD_BLOCK_ERROR) {
                    ssl_MapLowLevelError(SSL_ERROR_SOCKET_WRITE_FAILURE);
                    goto loser;
                }
                /* we got PR_WOULD_BLOCK_ERROR, which means none was sent. */
                sent = 0;
            }
            if (SSL_BUFFER_LEN(wrBuf) > (unsigned int)sent) {
                if (IS_DTLS(ss)) {
                    /* DTLS just says no in this case. No buffering */
                    PORT_SetError(PR_WOULD_BLOCK_ERROR);
                    goto loser;
                }
                /* now take all the remaining unsent new ciphertext and
                 * append it to the buffer of previously unsent ciphertext.
                 */
                rv = ssl_SaveWriteData(ss, SSL_BUFFER_BASE(wrBuf) + sent,
                                       SSL_BUFFER_LEN(wrBuf) - sent);
                if (rv != SECSuccess) {
                    /* presumably a memory error, SEC_ERROR_NO_MEMORY */
                    goto loser;
                }
            }
        }
        wrBuf->len = 0;
        totalSent += written;
    }
    return totalSent;

loser:
    /* Don't leave bits of buffer lying around. */
    wrBuf->len = 0;
    return -1;
}

#define SSL3_PENDING_HIGH_WATER 1024

/* Attempt to send the content of "in" in an SSL application_data record.
 * Returns "len" or -1 on failure.
 */
int
ssl3_SendApplicationData(sslSocket *ss, const unsigned char *in,
                         PRInt32 len, PRInt32 flags)
{
    PRInt32 totalSent = 0;
    PRInt32 discarded = 0;
    PRBool splitNeeded = PR_FALSE;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    /* These flags for internal use only */
    PORT_Assert(!(flags & ssl_SEND_FLAG_NO_RETRANSMIT));
    if (len < 0 || !in) {
        PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
        return -1;
    }

    if (ss->pendingBuf.len > SSL3_PENDING_HIGH_WATER &&
        !ssl_SocketIsBlocking(ss)) {
        PORT_Assert(!ssl_SocketIsBlocking(ss));
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return -1;
    }

    if (ss->appDataBuffered && len) {
        PORT_Assert(in[0] == (unsigned char)(ss->appDataBuffered));
        if (in[0] != (unsigned char)(ss->appDataBuffered)) {
            PORT_SetError(PR_INVALID_ARGUMENT_ERROR);
            return -1;
        }
        in++;
        len--;
        discarded = 1;
    }

    /* We will split the first byte of the record into its own record, as
     * explained in the documentation for SSL_CBC_RANDOM_IV in ssl.h.
     */
    if (len > 1 && ss->opt.cbcRandomIV &&
        ss->version < SSL_LIBRARY_VERSION_TLS_1_1 &&
        ss->ssl3.cwSpec->cipherDef->type == type_block /* CBC */) {
        splitNeeded = PR_TRUE;
    }

    while (len > totalSent) {
        PRInt32 sent, toSend;

        if (totalSent > 0) {
            /*
             * The thread yield is intended to give the reader thread a
             * chance to get some cycles while the writer thread is in
             * the middle of a large application data write.  (See
             * Bugzilla bug 127740, comment #1.)
             */
            ssl_ReleaseXmitBufLock(ss);
            PR_Sleep(PR_INTERVAL_NO_WAIT); /* PR_Yield(); */
            ssl_GetXmitBufLock(ss);
        }

        if (splitNeeded) {
            toSend = 1;
            splitNeeded = PR_FALSE;
        } else {
            toSend = PR_MIN(len - totalSent, MAX_FRAGMENT_LENGTH);
        }

        /*
         * Note that the 0 epoch is OK because flags will never require
         * its use, as guaranteed by the PORT_Assert above.
         */
        sent = ssl3_SendRecord(ss, NULL, ssl_ct_application_data,
                               in + totalSent, toSend, flags);
        if (sent < 0) {
            if (totalSent > 0 && PR_GetError() == PR_WOULD_BLOCK_ERROR) {
                PORT_Assert(ss->lastWriteBlocked);
                break;
            }
            return -1; /* error code set by ssl3_SendRecord */
        }
        totalSent += sent;
        if (ss->pendingBuf.len) {
            /* must be a non-blocking socket */
            PORT_Assert(!ssl_SocketIsBlocking(ss));
            PORT_Assert(ss->lastWriteBlocked);
            break;
        }
    }
    if (ss->pendingBuf.len) {
        /* Must be non-blocking. */
        PORT_Assert(!ssl_SocketIsBlocking(ss));
        if (totalSent > 0) {
            ss->appDataBuffered = 0x100 | in[totalSent - 1];
        }

        totalSent = totalSent + discarded - 1;
        if (totalSent <= 0) {
            PORT_SetError(PR_WOULD_BLOCK_ERROR);
            totalSent = SECFailure;
        }
        return totalSent;
    }
    ss->appDataBuffered = 0;
    return totalSent + discarded;
}

/* Attempt to send buffered handshake messages.
 * Always set sendBuf.len to 0, even when returning SECFailure.
 *
 * Depending on whether we are doing DTLS or not, this either calls
 *
 * - ssl3_FlushHandshakeMessages if non-DTLS
 * - dtls_FlushHandshakeMessages if DTLS
 *
 * Called from SSL3_SendAlert(), ssl3_SendChangeCipherSpecs(),
 *             ssl3_AppendHandshake(), ssl3_SendClientHello(),
 *             ssl3_SendHelloRequest(), ssl3_SendServerHelloDone(),
 *             ssl3_SendFinished(),
 */
SECStatus
ssl3_FlushHandshake(sslSocket *ss, PRInt32 flags)
{
    if (IS_DTLS(ss)) {
        return dtls_FlushHandshakeMessages(ss, flags);
    }
    return ssl3_FlushHandshakeMessages(ss, flags);
}

/* Attempt to send the content of sendBuf buffer in an SSL handshake record.
 * Always set sendBuf.len to 0, even when returning SECFailure.
 *
 * Called from ssl3_FlushHandshake
 */
static SECStatus
ssl3_FlushHandshakeMessages(sslSocket *ss, PRInt32 flags)
{
    static const PRInt32 allowedFlags = ssl_SEND_FLAG_FORCE_INTO_BUFFER;
    PRInt32 count = -1;
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    if (!ss->sec.ci.sendBuf.buf || !ss->sec.ci.sendBuf.len)
        return SECSuccess;

    /* only these flags are allowed */
    PORT_Assert(!(flags & ~allowedFlags));
    if ((flags & ~allowedFlags) != 0) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    count = ssl3_SendRecord(ss, NULL, ssl_ct_handshake,
                            ss->sec.ci.sendBuf.buf,
                            ss->sec.ci.sendBuf.len, flags);
    if (count < 0) {
        int err = PORT_GetError();
        PORT_Assert(err != PR_WOULD_BLOCK_ERROR);
        if (err == PR_WOULD_BLOCK_ERROR) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        }
        rv = SECFailure;
    } else if ((unsigned int)count < ss->sec.ci.sendBuf.len) {
        /* short write should never happen */
        PORT_Assert((unsigned int)count >= ss->sec.ci.sendBuf.len);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        rv = SECFailure;
    } else {
        rv = SECSuccess;
    }

    /* Whether we succeeded or failed, toss the old handshake data. */
    ss->sec.ci.sendBuf.len = 0;
    return rv;
}

/*
 * Called from ssl3_HandleAlert and from ssl3_HandleCertificate when
 * the remote client sends a negative response to our certificate request.
 * Returns SECFailure if the application has required client auth.
 *         SECSuccess otherwise.
 */
SECStatus
ssl3_HandleNoCertificate(sslSocket *ss)
{
    ssl3_CleanupPeerCerts(ss);

    /* If the server has required client-auth blindly but doesn't
     * actually look at the certificate it won't know that no
     * certificate was presented so we shutdown the socket to ensure
     * an error.  We only do this if we haven't already completed the
     * first handshake because if we're redoing the handshake we
     * know the server is paying attention to the certificate.
     */
    if ((ss->opt.requireCertificate == SSL_REQUIRE_ALWAYS) ||
        (!ss->firstHsDone &&
         (ss->opt.requireCertificate == SSL_REQUIRE_FIRST_HANDSHAKE))) {
        PRFileDesc *lower;

        ssl_UncacheSessionID(ss);

        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
            SSL3_SendAlert(ss, alert_fatal, certificate_required);
        } else {
            SSL3_SendAlert(ss, alert_fatal, bad_certificate);
        }

        lower = ss->fd->lower;
#ifdef _WIN32
        lower->methods->shutdown(lower, PR_SHUTDOWN_SEND);
#else
        lower->methods->shutdown(lower, PR_SHUTDOWN_BOTH);
#endif
        PORT_SetError(SSL_ERROR_NO_CERTIFICATE);
        return SECFailure;
    }
    return SECSuccess;
}

/************************************************************************
 * Alerts
 */

/*
** Acquires both handshake and XmitBuf locks.
** Called from: ssl3_IllegalParameter   <-
**              ssl3_HandshakeFailure   <-
**              ssl3_HandleAlert    <- ssl3_HandleRecord.
**              ssl3_HandleChangeCipherSpecs <- ssl3_HandleRecord
**              ssl3_ConsumeHandshakeVariable <-
**              ssl3_HandleHelloRequest <-
**              ssl3_HandleServerHello  <-
**              ssl3_HandleServerKeyExchange <-
**              ssl3_HandleCertificateRequest <-
**              ssl3_HandleServerHelloDone <-
**              ssl3_HandleClientHello  <-
**              ssl3_HandleV2ClientHello <-
**              ssl3_HandleCertificateVerify <-
**              ssl3_HandleClientKeyExchange <-
**              ssl3_HandleCertificate  <-
**              ssl3_HandleFinished <-
**              ssl3_HandleHandshakeMessage <-
**              ssl3_HandlePostHelloHandshakeMessage <-
**              ssl3_HandleRecord   <-
**
*/
SECStatus
SSL3_SendAlert(sslSocket *ss, SSL3AlertLevel level, SSL3AlertDescription desc)
{
    PRUint8 bytes[2];
    SECStatus rv;
    PRBool needHsLock = !ssl_HaveSSL3HandshakeLock(ss);

    /* Check that if I need the HS lock I also need the Xmit lock */
    PORT_Assert(!needHsLock || !ssl_HaveXmitBufLock(ss));

    SSL_TRC(3, ("%d: SSL3[%d]: send alert record, level=%d desc=%d",
                SSL_GETPID(), ss->fd, level, desc));

    bytes[0] = level;
    bytes[1] = desc;

    if (needHsLock) {
        ssl_GetSSL3HandshakeLock(ss);
    }
    if (level == alert_fatal) {
        if (ss->sec.ci.sid) {
            ssl_UncacheSessionID(ss);
        }
    }

    rv = tls13_SetAlertCipherSpec(ss);
    if (rv != SECSuccess) {
        if (needHsLock) {
            ssl_ReleaseSSL3HandshakeLock(ss);
        }
        return rv;
    }

    ssl_GetXmitBufLock(ss);
    rv = ssl3_FlushHandshake(ss, ssl_SEND_FLAG_FORCE_INTO_BUFFER);
    if (rv == SECSuccess) {
        PRInt32 sent;
        sent = ssl3_SendRecord(ss, NULL, ssl_ct_alert, bytes, 2,
                               (desc == no_certificate) ? ssl_SEND_FLAG_FORCE_INTO_BUFFER : 0);
        rv = (sent >= 0) ? SECSuccess : (SECStatus)sent;
    }
    if (level == alert_fatal) {
        ss->ssl3.fatalAlertSent = PR_TRUE;
    }
    ssl_ReleaseXmitBufLock(ss);
    if (needHsLock) {
        ssl_ReleaseSSL3HandshakeLock(ss);
    }
    if (rv == SECSuccess && ss->alertSentCallback) {
        SSLAlert alert = { level, desc };
        ss->alertSentCallback(ss->fd, ss->alertSentCallbackArg, &alert);
    }
    return rv; /* error set by ssl3_FlushHandshake or ssl3_SendRecord */
}

/*
 * Send illegal_parameter alert.  Set generic error number.
 */
static SECStatus
ssl3_IllegalParameter(sslSocket *ss)
{
    (void)SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
    PORT_SetError(ss->sec.isServer ? SSL_ERROR_BAD_CLIENT
                                   : SSL_ERROR_BAD_SERVER);
    return SECFailure;
}

/*
 * Send handshake_Failure alert.  Set generic error number.
 */
static SECStatus
ssl3_HandshakeFailure(sslSocket *ss)
{
    (void)SSL3_SendAlert(ss, alert_fatal, handshake_failure);
    PORT_SetError(ss->sec.isServer ? SSL_ERROR_BAD_CLIENT
                                   : SSL_ERROR_BAD_SERVER);
    return SECFailure;
}

void
ssl3_SendAlertForCertError(sslSocket *ss, PRErrorCode errCode)
{
    SSL3AlertDescription desc = bad_certificate;
    PRBool isTLS = ss->version >= SSL_LIBRARY_VERSION_3_1_TLS;

    switch (errCode) {
        case SEC_ERROR_LIBRARY_FAILURE:
            desc = unsupported_certificate;
            break;
        case SEC_ERROR_EXPIRED_CERTIFICATE:
            desc = certificate_expired;
            break;
        case SEC_ERROR_REVOKED_CERTIFICATE:
            desc = certificate_revoked;
            break;
        case SEC_ERROR_INADEQUATE_KEY_USAGE:
        case SEC_ERROR_INADEQUATE_CERT_TYPE:
            desc = certificate_unknown;
            break;
        case SEC_ERROR_UNTRUSTED_CERT:
            desc = isTLS ? access_denied : certificate_unknown;
            break;
        case SEC_ERROR_UNKNOWN_ISSUER:
        case SEC_ERROR_UNTRUSTED_ISSUER:
            desc = isTLS ? unknown_ca : certificate_unknown;
            break;
        case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
            desc = isTLS ? unknown_ca : certificate_expired;
            break;

        case SEC_ERROR_CERT_NOT_IN_NAME_SPACE:
        case SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID:
        case SEC_ERROR_CA_CERT_INVALID:
        case SEC_ERROR_BAD_SIGNATURE:
        default:
            desc = bad_certificate;
            break;
    }
    SSL_DBG(("%d: SSL3[%d]: peer certificate is no good: error=%d",
             SSL_GETPID(), ss->fd, errCode));

    (void)SSL3_SendAlert(ss, alert_fatal, desc);
}

/*
 * Send decode_error alert.  Set generic error number.
 */
SECStatus
ssl3_DecodeError(sslSocket *ss)
{
    (void)SSL3_SendAlert(ss, alert_fatal,
                         ss->version > SSL_LIBRARY_VERSION_3_0 ? decode_error
                                                               : illegal_parameter);
    PORT_SetError(ss->sec.isServer ? SSL_ERROR_BAD_CLIENT
                                   : SSL_ERROR_BAD_SERVER);
    return SECFailure;
}

/* Called from ssl3_HandleRecord.
** Caller must hold both RecvBuf and Handshake locks.
*/
static SECStatus
ssl3_HandleAlert(sslSocket *ss, sslBuffer *buf)
{
    SSL3AlertLevel level;
    SSL3AlertDescription desc;
    int error;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: SSL3[%d]: handle alert record", SSL_GETPID(), ss->fd));

    if (buf->len != 2) {
        (void)ssl3_DecodeError(ss);
        PORT_SetError(SSL_ERROR_RX_MALFORMED_ALERT);
        return SECFailure;
    }
    level = (SSL3AlertLevel)buf->buf[0];
    desc = (SSL3AlertDescription)buf->buf[1];
    buf->len = 0;
    SSL_TRC(5, ("%d: SSL3[%d] received alert, level = %d, description = %d",
                SSL_GETPID(), ss->fd, level, desc));

    if (ss->alertReceivedCallback) {
        SSLAlert alert = { level, desc };
        ss->alertReceivedCallback(ss->fd, ss->alertReceivedCallbackArg, &alert);
    }

    switch (desc) {
        case close_notify:
            ss->recvdCloseNotify = 1;
            error = SSL_ERROR_CLOSE_NOTIFY_ALERT;
            break;
        case unexpected_message:
            error = SSL_ERROR_HANDSHAKE_UNEXPECTED_ALERT;
            break;
        case bad_record_mac:
            error = SSL_ERROR_BAD_MAC_ALERT;
            break;
        case decryption_failed_RESERVED:
            error = SSL_ERROR_DECRYPTION_FAILED_ALERT;
            break;
        case record_overflow:
            error = SSL_ERROR_RECORD_OVERFLOW_ALERT;
            break;
        case decompression_failure:
            error = SSL_ERROR_DECOMPRESSION_FAILURE_ALERT;
            break;
        case handshake_failure:
            error = SSL_ERROR_HANDSHAKE_FAILURE_ALERT;
            break;
        case no_certificate:
            error = SSL_ERROR_NO_CERTIFICATE;
            break;
        case certificate_required:
            error = SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT;
            break;
        case bad_certificate:
            error = SSL_ERROR_BAD_CERT_ALERT;
            break;
        case unsupported_certificate:
            error = SSL_ERROR_UNSUPPORTED_CERT_ALERT;
            break;
        case certificate_revoked:
            error = SSL_ERROR_REVOKED_CERT_ALERT;
            break;
        case certificate_expired:
            error = SSL_ERROR_EXPIRED_CERT_ALERT;
            break;
        case certificate_unknown:
            error = SSL_ERROR_CERTIFICATE_UNKNOWN_ALERT;
            break;
        case illegal_parameter:
            error = SSL_ERROR_ILLEGAL_PARAMETER_ALERT;
            break;
        case inappropriate_fallback:
            error = SSL_ERROR_INAPPROPRIATE_FALLBACK_ALERT;
            break;

        /* All alerts below are TLS only. */
        case unknown_ca:
            error = SSL_ERROR_UNKNOWN_CA_ALERT;
            break;
        case access_denied:
            error = SSL_ERROR_ACCESS_DENIED_ALERT;
            break;
        case decode_error:
            error = SSL_ERROR_DECODE_ERROR_ALERT;
            break;
        case decrypt_error:
            error = SSL_ERROR_DECRYPT_ERROR_ALERT;
            break;
        case export_restriction:
            error = SSL_ERROR_EXPORT_RESTRICTION_ALERT;
            break;
        case protocol_version:
            error = SSL_ERROR_PROTOCOL_VERSION_ALERT;
            break;
        case insufficient_security:
            error = SSL_ERROR_INSUFFICIENT_SECURITY_ALERT;
            break;
        case internal_error:
            error = SSL_ERROR_INTERNAL_ERROR_ALERT;
            break;
        case user_canceled:
            error = SSL_ERROR_USER_CANCELED_ALERT;
            break;
        case no_renegotiation:
            error = SSL_ERROR_NO_RENEGOTIATION_ALERT;
            break;

        /* Alerts for TLS client hello extensions */
        case missing_extension:
            error = SSL_ERROR_MISSING_EXTENSION_ALERT;
            break;
        case unsupported_extension:
            error = SSL_ERROR_UNSUPPORTED_EXTENSION_ALERT;
            break;
        case certificate_unobtainable:
            error = SSL_ERROR_CERTIFICATE_UNOBTAINABLE_ALERT;
            break;
        case unrecognized_name:
            error = SSL_ERROR_UNRECOGNIZED_NAME_ALERT;
            break;
        case bad_certificate_status_response:
            error = SSL_ERROR_BAD_CERT_STATUS_RESPONSE_ALERT;
            break;
        case bad_certificate_hash_value:
            error = SSL_ERROR_BAD_CERT_HASH_VALUE_ALERT;
            break;
        case no_application_protocol:
            error = SSL_ERROR_NEXT_PROTOCOL_NO_PROTOCOL;
            break;
        case ech_required:
            error = SSL_ERROR_ECH_REQUIRED_ALERT;
            break;
        default:
            error = SSL_ERROR_RX_UNKNOWN_ALERT;
            break;
    }
    if ((ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) &&
        (ss->ssl3.hs.ws != wait_server_hello)) {
        /* TLS 1.3 requires all but "end of data" alerts to be
         * treated as fatal. */
        switch (desc) {
            case close_notify:
            case user_canceled:
                break;
            default:
                level = alert_fatal;
        }
    }
    if (level == alert_fatal) {
        ssl_UncacheSessionID(ss);
        if ((ss->ssl3.hs.ws == wait_server_hello) &&
            (desc == handshake_failure)) {
            /* XXX This is a hack.  We're assuming that any handshake failure
             * XXX on the client hello is a failure to match ciphers.
             */
            error = SSL_ERROR_NO_CYPHER_OVERLAP;
        }
        PORT_SetError(error);
        return SECFailure;
    }
    if ((desc == no_certificate) && (ss->ssl3.hs.ws == wait_client_cert)) {
        /* I'm a server. I've requested a client cert. He hasn't got one. */
        SECStatus rv;

        PORT_Assert(ss->sec.isServer);
        ss->ssl3.hs.ws = wait_client_key;
        rv = ssl3_HandleNoCertificate(ss);
        return rv;
    }
    return SECSuccess;
}

/*
 * Change Cipher Specs
 * Called from ssl3_HandleServerHelloDone,
 *             ssl3_HandleClientHello,
 * and         ssl3_HandleFinished
 *
 * Acquires and releases spec write lock, to protect switching the current
 * and pending write spec pointers.
 */

SECStatus
ssl3_SendChangeCipherSpecsInt(sslSocket *ss)
{
    PRUint8 change = change_cipher_spec_choice;
    SECStatus rv;

    SSL_TRC(3, ("%d: SSL3[%d]: send change_cipher_spec record",
                SSL_GETPID(), ss->fd));

    rv = ssl3_FlushHandshake(ss, ssl_SEND_FLAG_FORCE_INTO_BUFFER);
    if (rv != SECSuccess) {
        return SECFailure; /* error code set by ssl3_FlushHandshake */
    }

    if (!IS_DTLS(ss)) {
        PRInt32 sent;
        sent = ssl3_SendRecord(ss, NULL, ssl_ct_change_cipher_spec,
                               &change, 1, ssl_SEND_FLAG_FORCE_INTO_BUFFER);
        if (sent < 0) {
            return SECFailure; /* error code set by ssl3_SendRecord */
        }
    } else {
        rv = dtls_QueueMessage(ss, ssl_ct_change_cipher_spec, &change, 1);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    return SECSuccess;
}

static SECStatus
ssl3_SendChangeCipherSpecs(sslSocket *ss)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    rv = ssl3_SendChangeCipherSpecsInt(ss);
    if (rv != SECSuccess) {
        return rv; /* Error code set. */
    }

    /* swap the pending and current write specs. */
    ssl_GetSpecWriteLock(ss); /**************************************/

    ssl_CipherSpecRelease(ss->ssl3.cwSpec);
    ss->ssl3.cwSpec = ss->ssl3.pwSpec;
    ss->ssl3.pwSpec = NULL;

    SSL_TRC(3, ("%d: SSL3[%d] Set Current Write Cipher Suite to Pending",
                SSL_GETPID(), ss->fd));

    /* With DTLS, we need to set a holddown timer in case the final
     * message got lost */
    if (IS_DTLS(ss) && ss->ssl3.crSpec->epoch == ss->ssl3.cwSpec->epoch) {
        rv = dtls_StartHolddownTimer(ss);
    }
    ssl_ReleaseSpecWriteLock(ss); /**************************************/

    return rv;
}

/* Called from ssl3_HandleRecord.
** Caller must hold both RecvBuf and Handshake locks.
*
* Acquires and releases spec write lock, to protect switching the current
* and pending write spec pointers.
*/
static SECStatus
ssl3_HandleChangeCipherSpecs(sslSocket *ss, sslBuffer *buf)
{
    SSL3WaitState ws = ss->ssl3.hs.ws;
    SSL3ChangeCipherSpecChoice change;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: SSL3[%d]: handle change_cipher_spec record",
                SSL_GETPID(), ss->fd));

    /* For DTLS: Ignore this if we aren't expecting it.  Don't kill a connection
     *           as a result of receiving trash.
     * For TLS: Maybe ignore, but only after checking format. */
    if (ws != wait_change_cipher && IS_DTLS(ss)) {
        /* Ignore this because it's out of order. */
        SSL_TRC(3, ("%d: SSL3[%d]: discard out of order "
                    "DTLS change_cipher_spec",
                    SSL_GETPID(), ss->fd));
        buf->len = 0;
        return SECSuccess;
    }

    /* Handshake messages should not span ChangeCipherSpec. */
    if (ss->ssl3.hs.header_bytes) {
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CHANGE_CIPHER);
        return SECFailure;
    }
    if (buf->len != 1) {
        (void)ssl3_DecodeError(ss);
        PORT_SetError(SSL_ERROR_RX_MALFORMED_CHANGE_CIPHER);
        return SECFailure;
    }
    change = (SSL3ChangeCipherSpecChoice)buf->buf[0];
    if (change != change_cipher_spec_choice) {
        /* illegal_parameter is correct here for both SSL3 and TLS. */
        (void)ssl3_IllegalParameter(ss);
        PORT_SetError(SSL_ERROR_RX_MALFORMED_CHANGE_CIPHER);
        return SECFailure;
    }

    buf->len = 0;
    if (ws != wait_change_cipher) {
        /* Ignore a CCS for TLS 1.3. This only happens if the server sends a
         * HelloRetryRequest.  In other cases, the CCS will fail decryption and
         * will be discarded by ssl3_HandleRecord(). */
        if (ws == wait_server_hello &&
            ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
            ss->ssl3.hs.helloRetry) {
            PORT_Assert(!ss->sec.isServer);
            return SECSuccess;
        }
        /* Note: For a server, we can't test ss->ssl3.hs.helloRetry or
         * ss->version because the server might be stateless (and so it won't
         * have set either value yet). Set a flag so that at least we will
         * guarantee that the server will treat any ClientHello properly. */
        if (ws == wait_client_hello &&
            ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3 &&
            !ss->ssl3.hs.receivedCcs) {
            PORT_Assert(ss->sec.isServer);
            ss->ssl3.hs.receivedCcs = PR_TRUE;
            return SECSuccess;
        }
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CHANGE_CIPHER);
        return SECFailure;
    }

    SSL_TRC(3, ("%d: SSL3[%d] Set Current Read Cipher Suite to Pending",
                SSL_GETPID(), ss->fd));
    ssl_GetSpecWriteLock(ss); /*************************************/
    PORT_Assert(ss->ssl3.prSpec);
    ssl_CipherSpecRelease(ss->ssl3.crSpec);
    ss->ssl3.crSpec = ss->ssl3.prSpec;
    ss->ssl3.prSpec = NULL;
    ssl_ReleaseSpecWriteLock(ss); /*************************************/

    ss->ssl3.hs.ws = wait_finished;
    return SECSuccess;
}

static CK_MECHANISM_TYPE
ssl3_GetMgfMechanismByHashType(SSLHashType hash)
{
    switch (hash) {
        case ssl_hash_sha256:
            return CKG_MGF1_SHA256;
        case ssl_hash_sha384:
            return CKG_MGF1_SHA384;
        case ssl_hash_sha512:
            return CKG_MGF1_SHA512;
        default:
            PORT_Assert(0);
    }
    return CKG_MGF1_SHA256;
}

/* Function valid for >= TLS 1.2, only. */
static CK_MECHANISM_TYPE
ssl3_GetHashMechanismByHashType(SSLHashType hashType)
{
    switch (hashType) {
        case ssl_hash_sha512:
            return CKM_SHA512;
        case ssl_hash_sha384:
            return CKM_SHA384;
        case ssl_hash_sha256:
        case ssl_hash_none:
            /* ssl_hash_none is for pre-1.2 suites, which use SHA-256. */
            return CKM_SHA256;
        case ssl_hash_sha1:
            return CKM_SHA_1;
        default:
            PORT_Assert(0);
    }
    return CKM_SHA256;
}

/* Function valid for >= TLS 1.2, only. */
static CK_MECHANISM_TYPE
ssl3_GetPrfHashMechanism(sslSocket *ss)
{
    return ssl3_GetHashMechanismByHashType(ss->ssl3.hs.suite_def->prf_hash);
}

static SSLHashType
ssl3_GetSuitePrfHash(sslSocket *ss)
{
    /* ssl_hash_none is for pre-1.2 suites, which use SHA-256. */
    if (ss->ssl3.hs.suite_def->prf_hash == ssl_hash_none) {
        return ssl_hash_sha256;
    }
    return ss->ssl3.hs.suite_def->prf_hash;
}

/* This method completes the derivation of the MS from the PMS.
**
** 1. Derive the MS, if possible, else return an error.
**
** 2. Check the version if |pms_version| is non-zero and if wrong,
**    return an error.
**
** 3. If |msp| is nonzero, return MS in |*msp|.

** Called from:
**   ssl3_ComputeMasterSecretInt
**   tls_ComputeExtendedMasterSecretInt
*/
static SECStatus
ssl3_ComputeMasterSecretFinish(sslSocket *ss,
                               CK_MECHANISM_TYPE master_derive,
                               CK_MECHANISM_TYPE key_derive,
                               CK_VERSION *pms_version,
                               SECItem *params, CK_FLAGS keyFlags,
                               PK11SymKey *pms, PK11SymKey **msp)
{
    PK11SymKey *ms = NULL;

    ms = PK11_DeriveWithFlags(pms, master_derive,
                              params, key_derive,
                              CKA_DERIVE, 0, keyFlags);
    if (!ms) {
        ssl_MapLowLevelError(SSL_ERROR_SESSION_KEY_GEN_FAILURE);
        return SECFailure;
    }

    if (pms_version && ss->opt.detectRollBack) {
        SSL3ProtocolVersion client_version;
        client_version = pms_version->major << 8 | pms_version->minor;

        if (IS_DTLS(ss)) {
            client_version = dtls_DTLSVersionToTLSVersion(client_version);
        }

        if (client_version != ss->clientHelloVersion) {
            /* Destroy MS.  Version roll-back detected. */
            PK11_FreeSymKey(ms);
            ssl_MapLowLevelError(SSL_ERROR_SESSION_KEY_GEN_FAILURE);
            return SECFailure;
        }
    }

    if (msp) {
        *msp = ms;
    } else {
        PK11_FreeSymKey(ms);
    }

    return SECSuccess;
}

/*  Compute the ordinary (pre draft-ietf-tls-session-hash) master
 ** secret and return it in |*msp|.
 **
 ** Called from: ssl3_ComputeMasterSecret
 */
static SECStatus
ssl3_ComputeMasterSecretInt(sslSocket *ss, PK11SymKey *pms,
                            PK11SymKey **msp)
{
    PRBool isTLS = (PRBool)(ss->version > SSL_LIBRARY_VERSION_3_0);
    PRBool isTLS12 = (PRBool)(ss->version >= SSL_LIBRARY_VERSION_TLS_1_2);
    /*
     * Whenever isDH is true, we need to use CKM_TLS_MASTER_KEY_DERIVE_DH
     * which, unlike CKM_TLS_MASTER_KEY_DERIVE, converts arbitrary size
     * data into a 48-byte value, and does not expect to return the version.
     */
    PRBool isDH = (PRBool)((ss->ssl3.hs.kea_def->exchKeyType == ssl_kea_dh) ||
                           (ss->ssl3.hs.kea_def->exchKeyType == ssl_kea_ecdh) ||
                           (ss->ssl3.hs.kea_def->exchKeyType == ssl_kea_ecdh_hybrid));
    CK_MECHANISM_TYPE master_derive;
    CK_MECHANISM_TYPE key_derive;
    SECItem params;
    CK_FLAGS keyFlags;
    CK_VERSION pms_version;
    CK_VERSION *pms_version_ptr = NULL;
    /* master_params may be used as a CK_SSL3_MASTER_KEY_DERIVE_PARAMS */
    CK_TLS12_MASTER_KEY_DERIVE_PARAMS master_params;
    unsigned int master_params_len;

    /* if we are using TLS and we aren't using the extended master secret,
     * and SEC_OID_TLS_REQUIRE_EMS policy is true, fail. The caller will
     * send an alert (eventually). In the RSA Server case, the alert
     * won't happen until Finish time because the upper level code
     * can't tell a difference between this failure and an RSA decrypt
     * failure, so it will proceed with a faux key */
    if (isTLS) {
        PRUint32 policy;
        SECStatus rv;

        /* first fetch the policy for this algorithm */
        rv = NSS_GetAlgorithmPolicy(SEC_OID_TLS_REQUIRE_EMS, &policy);
        /* we only look at the policy if we can fetch it. */
        if ((rv == SECSuccess) && (policy & NSS_USE_ALG_IN_SSL_KX)) {
            /* just set the error, we don't want to map any errors
             * set by NSS_GetAlgorithmPolicy here */
            PORT_SetError(SSL_ERROR_MISSING_EXTENDED_MASTER_SECRET);
            return SECFailure;
        }
    }

    if (isTLS12) {
        if (isDH)
            master_derive = CKM_TLS12_MASTER_KEY_DERIVE_DH;
        else
            master_derive = CKM_TLS12_MASTER_KEY_DERIVE;
        key_derive = CKM_TLS12_KEY_AND_MAC_DERIVE;
        keyFlags = CKF_SIGN | CKF_VERIFY;
    } else if (isTLS) {
        if (isDH)
            master_derive = CKM_TLS_MASTER_KEY_DERIVE_DH;
        else
            master_derive = CKM_TLS_MASTER_KEY_DERIVE;
        key_derive = CKM_TLS_KEY_AND_MAC_DERIVE;
        keyFlags = CKF_SIGN | CKF_VERIFY;
    } else {
        if (isDH)
            master_derive = CKM_SSL3_MASTER_KEY_DERIVE_DH;
        else
            master_derive = CKM_SSL3_MASTER_KEY_DERIVE;
        key_derive = CKM_SSL3_KEY_AND_MAC_DERIVE;
        keyFlags = 0;
    }

    if (!isDH) {
        pms_version_ptr = &pms_version;
    }

    master_params.pVersion = pms_version_ptr;
    master_params.RandomInfo.pClientRandom = ss->ssl3.hs.client_random;
    master_params.RandomInfo.ulClientRandomLen = SSL3_RANDOM_LENGTH;
    master_params.RandomInfo.pServerRandom = ss->ssl3.hs.server_random;
    master_params.RandomInfo.ulServerRandomLen = SSL3_RANDOM_LENGTH;
    if (isTLS12) {
        master_params.prfHashMechanism = ssl3_GetPrfHashMechanism(ss);
        master_params_len = sizeof(CK_TLS12_MASTER_KEY_DERIVE_PARAMS);
    } else {
        /* prfHashMechanism is not relevant with this PRF */
        master_params_len = sizeof(CK_SSL3_MASTER_KEY_DERIVE_PARAMS);
    }

    params.data = (unsigned char *)&master_params;
    params.len = master_params_len;

    return ssl3_ComputeMasterSecretFinish(ss, master_derive, key_derive,
                                          pms_version_ptr, &params,
                                          keyFlags, pms, msp);
}

/* Compute the draft-ietf-tls-session-hash master
** secret and return it in |*msp|.
**
** Called from: ssl3_ComputeMasterSecret
*/
static SECStatus
tls_ComputeExtendedMasterSecretInt(sslSocket *ss, PK11SymKey *pms,
                                   PK11SymKey **msp)
{
    ssl3CipherSpec *pwSpec = ss->ssl3.pwSpec;
    CK_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_PARAMS extended_master_params;
    SSL3Hashes hashes;

    /*
     * Determine whether to use the DH/ECDH or RSA derivation modes.
     */
    /*
     * TODO(ekr@rtfm.com): Verify that the slot can handle this key expansion
     * mode. Bug 1198298 */
    PRBool isDH = (PRBool)((ss->ssl3.hs.kea_def->exchKeyType == ssl_kea_dh) ||
                           (ss->ssl3.hs.kea_def->exchKeyType == ssl_kea_ecdh) ||
                           (ss->ssl3.hs.kea_def->exchKeyType == ssl_kea_ecdh_hybrid));
    CK_MECHANISM_TYPE master_derive;
    CK_MECHANISM_TYPE key_derive;
    SECItem params;
    const CK_FLAGS keyFlags = CKF_SIGN | CKF_VERIFY;
    CK_VERSION pms_version;
    CK_VERSION *pms_version_ptr = NULL;
    SECStatus rv;

    rv = ssl3_ComputeHandshakeHashes(ss, pwSpec, &hashes, 0);
    if (rv != SECSuccess) {
        PORT_Assert(0); /* Should never fail */
        ssl_MapLowLevelError(SSL_ERROR_SESSION_KEY_GEN_FAILURE);
        return SECFailure;
    }

    if (isDH) {
        master_derive = CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_DH;
    } else {
        master_derive = CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE;
        pms_version_ptr = &pms_version;
    }

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_2) {
        /* TLS 1.2+ */
        extended_master_params.prfHashMechanism = ssl3_GetPrfHashMechanism(ss);
        key_derive = CKM_TLS12_KEY_AND_MAC_DERIVE;
    } else {
        /* TLS < 1.2 */
        extended_master_params.prfHashMechanism = CKM_TLS_PRF;
        key_derive = CKM_TLS_KEY_AND_MAC_DERIVE;
    }

    extended_master_params.pVersion = pms_version_ptr;
    extended_master_params.pSessionHash = hashes.u.raw;
    extended_master_params.ulSessionHashLen = hashes.len;

    params.data = (unsigned char *)&extended_master_params;
    params.len = sizeof extended_master_params;

    return ssl3_ComputeMasterSecretFinish(ss, master_derive, key_derive,
                                          pms_version_ptr, &params,
                                          keyFlags, pms, msp);
}

/* Wrapper method to compute the master secret and return it in |*msp|.
**
** Called from ssl3_ComputeMasterSecret
*/
static SECStatus
ssl3_ComputeMasterSecret(sslSocket *ss, PK11SymKey *pms,
                         PK11SymKey **msp)
{
    PORT_Assert(pms != NULL);
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ssl3_ExtensionNegotiated(ss, ssl_extended_master_secret_xtn)) {
        return tls_ComputeExtendedMasterSecretInt(ss, pms, msp);
    } else {
        return ssl3_ComputeMasterSecretInt(ss, pms, msp);
    }
}

/*
 * Derive encryption and MAC Keys (and IVs) from master secret
 * Sets a useful error code when returning SECFailure.
 *
 * Called only from ssl3_InitPendingCipherSpec(),
 * which in turn is called from
 *              ssl3_SendRSAClientKeyExchange    (for Full handshake)
 *              ssl3_SendDHClientKeyExchange     (for Full handshake)
 *              ssl3_HandleClientKeyExchange    (for Full handshake)
 *              ssl3_HandleServerHello          (for session restart)
 *              ssl3_HandleClientHello          (for session restart)
 * Caller MUST hold the specWriteLock, and SSL3HandshakeLock.
 * ssl3_InitPendingCipherSpec does that.
 *
 */
static SECStatus
ssl3_DeriveConnectionKeys(sslSocket *ss, PK11SymKey *masterSecret)
{
    ssl3CipherSpec *pwSpec = ss->ssl3.pwSpec;
    ssl3CipherSpec *prSpec = ss->ssl3.prSpec;
    ssl3CipherSpec *clientSpec;
    ssl3CipherSpec *serverSpec;
    PRBool isTLS = (PRBool)(ss->version > SSL_LIBRARY_VERSION_3_0);
    PRBool isTLS12 =
        (PRBool)(isTLS && ss->version >= SSL_LIBRARY_VERSION_TLS_1_2);
    const ssl3BulkCipherDef *cipher_def = pwSpec->cipherDef;
    PK11SlotInfo *slot = NULL;
    PK11SymKey *derivedKeyHandle = NULL;
    void *pwArg = ss->pkcs11PinArg;
    int keySize;
    CK_TLS12_KEY_MAT_PARAMS key_material_params; /* may be used as a
                                                  * CK_SSL3_KEY_MAT_PARAMS */
    unsigned int key_material_params_len;
    CK_SSL3_KEY_MAT_OUT returnedKeys;
    CK_MECHANISM_TYPE key_derive;
    CK_MECHANISM_TYPE bulk_mechanism;
    SSLCipherAlgorithm calg;
    SECItem params;
    PRBool skipKeysAndIVs = (PRBool)(cipher_def->calg == ssl_calg_null);

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSpecWriteLock(ss));
    PORT_Assert(masterSecret);

    /* These functions operate in terms of who is writing specs. */
    if (ss->sec.isServer) {
        clientSpec = prSpec;
        serverSpec = pwSpec;
    } else {
        clientSpec = pwSpec;
        serverSpec = prSpec;
    }

    /*
     * generate the key material
     */
    if (cipher_def->type == type_block &&
        ss->version >= SSL_LIBRARY_VERSION_TLS_1_1) {
        /* Block ciphers in >= TLS 1.1 use a per-record, explicit IV. */
        key_material_params.ulIVSizeInBits = 0;
        PORT_Memset(clientSpec->keyMaterial.iv, 0, cipher_def->iv_size);
        PORT_Memset(serverSpec->keyMaterial.iv, 0, cipher_def->iv_size);
    }

    key_material_params.bIsExport = PR_FALSE;
    key_material_params.RandomInfo.pClientRandom = ss->ssl3.hs.client_random;
    key_material_params.RandomInfo.ulClientRandomLen = SSL3_RANDOM_LENGTH;
    key_material_params.RandomInfo.pServerRandom = ss->ssl3.hs.server_random;
    key_material_params.RandomInfo.ulServerRandomLen = SSL3_RANDOM_LENGTH;
    key_material_params.pReturnedKeyMaterial = &returnedKeys;

    if (skipKeysAndIVs) {
        keySize = 0;
        returnedKeys.pIVClient = NULL;
        returnedKeys.pIVServer = NULL;
        key_material_params.ulKeySizeInBits = 0;
        key_material_params.ulIVSizeInBits = 0;
    } else {
        keySize = cipher_def->key_size;
        returnedKeys.pIVClient = clientSpec->keyMaterial.iv;
        returnedKeys.pIVServer = serverSpec->keyMaterial.iv;
        key_material_params.ulKeySizeInBits = cipher_def->secret_key_size * BPB;
        key_material_params.ulIVSizeInBits = cipher_def->iv_size * BPB;
    }
    key_material_params.ulMacSizeInBits = pwSpec->macDef->mac_size * BPB;

    calg = cipher_def->calg;
    bulk_mechanism = ssl3_Alg2Mech(calg);

    if (isTLS12) {
        key_derive = CKM_TLS12_KEY_AND_MAC_DERIVE;
        key_material_params.prfHashMechanism = ssl3_GetPrfHashMechanism(ss);
        key_material_params_len = sizeof(CK_TLS12_KEY_MAT_PARAMS);
    } else if (isTLS) {
        key_derive = CKM_TLS_KEY_AND_MAC_DERIVE;
        key_material_params_len = sizeof(CK_SSL3_KEY_MAT_PARAMS);
    } else {
        key_derive = CKM_SSL3_KEY_AND_MAC_DERIVE;
        key_material_params_len = sizeof(CK_SSL3_KEY_MAT_PARAMS);
    }

    params.data = (unsigned char *)&key_material_params;
    params.len = key_material_params_len;

    /* CKM_SSL3_KEY_AND_MAC_DERIVE is defined to set ENCRYPT, DECRYPT, and
     * DERIVE by DEFAULT */
    derivedKeyHandle = PK11_Derive(masterSecret, key_derive, &params,
                                   bulk_mechanism, CKA_ENCRYPT, keySize);
    if (!derivedKeyHandle) {
        ssl_MapLowLevelError(SSL_ERROR_SESSION_KEY_GEN_FAILURE);
        return SECFailure;
    }
    /* we really should use the actual mac'ing mechanism here, but we
     * don't because these types are used to map keytype anyway and both
     * mac's map to the same keytype.
     */
    slot = PK11_GetSlotFromKey(derivedKeyHandle);

    PK11_FreeSlot(slot); /* slot is held until the key is freed */
    clientSpec->keyMaterial.macKey =
        PK11_SymKeyFromHandle(slot, derivedKeyHandle, PK11_OriginDerive,
                              CKM_SSL3_SHA1_MAC, returnedKeys.hClientMacSecret,
                              PR_TRUE, pwArg);
    if (clientSpec->keyMaterial.macKey == NULL) {
        goto loser; /* loser sets err */
    }
    serverSpec->keyMaterial.macKey =
        PK11_SymKeyFromHandle(slot, derivedKeyHandle, PK11_OriginDerive,
                              CKM_SSL3_SHA1_MAC, returnedKeys.hServerMacSecret,
                              PR_TRUE, pwArg);
    if (serverSpec->keyMaterial.macKey == NULL) {
        goto loser; /* loser sets err */
    }
    if (!skipKeysAndIVs) {
        clientSpec->keyMaterial.key =
            PK11_SymKeyFromHandle(slot, derivedKeyHandle, PK11_OriginDerive,
                                  bulk_mechanism, returnedKeys.hClientKey,
                                  PR_TRUE, pwArg);
        if (clientSpec->keyMaterial.key == NULL) {
            goto loser; /* loser sets err */
        }
        serverSpec->keyMaterial.key =
            PK11_SymKeyFromHandle(slot, derivedKeyHandle, PK11_OriginDerive,
                                  bulk_mechanism, returnedKeys.hServerKey,
                                  PR_TRUE, pwArg);
        if (serverSpec->keyMaterial.key == NULL) {
            goto loser; /* loser sets err */
        }
    }
    PK11_FreeSymKey(derivedKeyHandle);
    return SECSuccess;

loser:
    PK11_FreeSymKey(derivedKeyHandle);
    ssl_MapLowLevelError(SSL_ERROR_SESSION_KEY_GEN_FAILURE);
    return SECFailure;
}

void
ssl3_CoalesceEchHandshakeHashes(sslSocket *ss)
{
    /* |sha| contains the CHOuter transcript, which is the singular
     * transcript if not doing ECH. If the server responded with 1.2,
     * contexts are not yet initialized. */
    if (ss->ssl3.hs.echAccepted) {
        if (ss->ssl3.hs.sha) {
            PORT_Assert(ss->ssl3.hs.shaEchInner);
            PK11_DestroyContext(ss->ssl3.hs.sha, PR_TRUE);
            ss->ssl3.hs.sha = ss->ssl3.hs.shaEchInner;
            ss->ssl3.hs.shaEchInner = NULL;
        }
    } else {
        if (ss->ssl3.hs.shaEchInner) {
            PK11_DestroyContext(ss->ssl3.hs.shaEchInner, PR_TRUE);
            ss->ssl3.hs.shaEchInner = NULL;
        }
    }
}

/* ssl3_InitHandshakeHashes creates handshake hash contexts and hashes in
 * buffered messages in ss->ssl3.hs.messages. Called from
 * ssl3_NegotiateCipherSuite(), tls13_HandleClientHelloPart2(),
 * and ssl3_HandleServerHello. */
SECStatus
ssl3_InitHandshakeHashes(sslSocket *ss)
{
    SSL_TRC(30, ("%d: SSL3[%d]: start handshake hashes", SSL_GETPID(), ss->fd));

    PORT_Assert(ss->ssl3.hs.hashType == handshake_hash_unknown);
    if (ss->version == SSL_LIBRARY_VERSION_TLS_1_2) {
        ss->ssl3.hs.hashType = handshake_hash_record;
    } else {
        PORT_Assert(!ss->ssl3.hs.md5 && !ss->ssl3.hs.sha);
        /*
         * note: We should probably lookup an SSL3 slot for these
         * handshake hashes in hopes that we wind up with the same slots
         * that the master secret will wind up in ...
         */
        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
            /* determine the hash from the prf */
            const SECOidData *hash_oid =
                SECOID_FindOIDByMechanism(ssl3_GetPrfHashMechanism(ss));

            /* Get the PKCS #11 mechanism for the Hash from the cipher suite (prf_hash)
             * Convert that to the OidTag. We can then use that OidTag to create our
             * PK11Context */
            PORT_Assert(hash_oid != NULL);
            if (hash_oid == NULL) {
                ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
                return SECFailure;
            }

            ss->ssl3.hs.sha = PK11_CreateDigestContext(hash_oid->offset);
            if (ss->ssl3.hs.sha == NULL) {
                ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
                return SECFailure;
            }
            ss->ssl3.hs.hashType = handshake_hash_single;
            if (PK11_DigestBegin(ss->ssl3.hs.sha) != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
                return SECFailure;
            }

            /* Transcript hash used on ECH client. */
            if (!ss->sec.isServer && ss->ssl3.hs.echHpkeCtx) {
                ss->ssl3.hs.shaEchInner = PK11_CreateDigestContext(hash_oid->offset);
                if (ss->ssl3.hs.shaEchInner == NULL) {
                    ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
                    return SECFailure;
                }
                if (PK11_DigestBegin(ss->ssl3.hs.shaEchInner) != SECSuccess) {
                    ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
                    return SECFailure;
                }
            }
        } else {
            /* Both ss->ssl3.hs.md5 and ss->ssl3.hs.sha should be NULL or
             * created successfully. */
            ss->ssl3.hs.md5 = PK11_CreateDigestContext(SEC_OID_MD5);
            if (ss->ssl3.hs.md5 == NULL) {
                ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
                return SECFailure;
            }
            ss->ssl3.hs.sha = PK11_CreateDigestContext(SEC_OID_SHA1);
            if (ss->ssl3.hs.sha == NULL) {
                PK11_DestroyContext(ss->ssl3.hs.md5, PR_TRUE);
                ss->ssl3.hs.md5 = NULL;
                ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
                return SECFailure;
            }
            ss->ssl3.hs.hashType = handshake_hash_combo;

            if (PK11_DigestBegin(ss->ssl3.hs.md5) != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
                return SECFailure;
            }
            if (PK11_DigestBegin(ss->ssl3.hs.sha) != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
                return SECFailure;
            }
        }
    }

    if (ss->ssl3.hs.hashType != handshake_hash_record &&
        ss->ssl3.hs.messages.len > 0) {
        /* When doing ECH, ssl3_UpdateHandshakeHashes will store outer messages
         * into the both the outer and inner transcripts.
         * ssl3_UpdateDefaultHandshakeHashes uses the default context which is
         * the outer when doing client ECH. For ECH shared-mode or backend
         * servers only the hs.messages buffer is used. */
        if (ssl3_UpdateDefaultHandshakeHashes(ss, ss->ssl3.hs.messages.buf,
                                              ss->ssl3.hs.messages.len) != SECSuccess) {
            return SECFailure;
        }
        /* When doing ECH, deriving the accept_confirmation value requires all
         * messages up to and including the ServerHello
         * (see draft-ietf-tls-esni-14, Section 7.2).
         *
         * Don't free the transcript buffer until confirmation calculation. */
        if (!ss->ssl3.hs.echHpkeCtx && !ss->opt.enableTls13BackendEch) {
            sslBuffer_Clear(&ss->ssl3.hs.messages);
        }
    }
    if (ss->ssl3.hs.shaEchInner &&
        ss->ssl3.hs.echInnerMessages.len > 0) {
        if (PK11_DigestOp(ss->ssl3.hs.shaEchInner, ss->ssl3.hs.echInnerMessages.buf,
                          ss->ssl3.hs.echInnerMessages.len) != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
            return SECFailure;
        }
        if (!ss->ssl3.hs.echHpkeCtx) {
            sslBuffer_Clear(&ss->ssl3.hs.echInnerMessages);
        }
    }

    return SECSuccess;
}

void
ssl3_RestartHandshakeHashes(sslSocket *ss)
{
    SSL_TRC(30, ("%d: SSL3[%d]: reset handshake hashes",
                 SSL_GETPID(), ss->fd));
    ss->ssl3.hs.hashType = handshake_hash_unknown;
    ss->ssl3.hs.messages.len = 0;
    ss->ssl3.hs.echInnerMessages.len = 0;
    if (ss->ssl3.hs.md5) {
        PK11_DestroyContext(ss->ssl3.hs.md5, PR_TRUE);
        ss->ssl3.hs.md5 = NULL;
    }
    if (ss->ssl3.hs.sha) {
        PK11_DestroyContext(ss->ssl3.hs.sha, PR_TRUE);
        ss->ssl3.hs.sha = NULL;
    }
    if (ss->ssl3.hs.shaEchInner) {
        PK11_DestroyContext(ss->ssl3.hs.shaEchInner, PR_TRUE);
        ss->ssl3.hs.shaEchInner = NULL;
    }
    if (ss->ssl3.hs.shaPostHandshake) {
        PK11_DestroyContext(ss->ssl3.hs.shaPostHandshake, PR_TRUE);
        ss->ssl3.hs.shaPostHandshake = NULL;
    }
}

/* Add the provided bytes to the handshake hash context. When doing
 * TLS 1.3 ECH, |target| may be provided to specify only the inner/outer
 * transcript, else the input is added to both contexts. This happens
 * only on the client. On the server, only the default context is used. */
SECStatus
ssl3_UpdateHandshakeHashesInt(sslSocket *ss, const unsigned char *b,
                              unsigned int l, sslBuffer *target)
{

    SECStatus rv = SECSuccess;
    PRBool explicit = (target != NULL);
    PRBool appendToEchInner = !ss->sec.isServer &&
                              ss->ssl3.hs.echHpkeCtx &&
                              !explicit;
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(target != &ss->ssl3.hs.echInnerMessages ||
                !ss->sec.isServer);

    if (target == NULL) {
        /* Default context. */
        target = &ss->ssl3.hs.messages;
    }
    /* With TLS 1.3, and versions TLS.1.1 and older, we keep the hash(es)
     * always up to date. However, we must initially buffer the handshake
     * messages, until we know what to do.
     * If ss->ssl3.hs.hashType != handshake_hash_unknown,
     * it means we know what to do. We calculate (hash our input),
     * and we stop appending to the buffer.
     *
     * With TLS 1.2, we always append all handshake messages,
     * and never update the hash, because the hash function we must use for
     * certificate_verify might be different from the hash function we use
     * when signing other handshake hashes. */
    if (ss->ssl3.hs.hashType == handshake_hash_unknown ||
        ss->ssl3.hs.hashType == handshake_hash_record) {
        rv = sslBuffer_Append(target, b, l);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        if (appendToEchInner) {
            return sslBuffer_Append(&ss->ssl3.hs.echInnerMessages, b, l);
        }
        return SECSuccess;
    }

    PRINT_BUF(90, (ss, "handshake hash input:", b, l));

    if (ss->ssl3.hs.hashType == handshake_hash_single) {
        PORT_Assert(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
        if (target == &ss->ssl3.hs.messages) {
            rv = PK11_DigestOp(ss->ssl3.hs.sha, b, l);
            if (rv != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
                return rv;
            }
        }
        if (ss->ssl3.hs.shaEchInner &&
            (target == &ss->ssl3.hs.echInnerMessages || !explicit)) {
            rv = PK11_DigestOp(ss->ssl3.hs.shaEchInner, b, l);
            if (rv != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
                return rv;
            }
        }
    } else if (ss->ssl3.hs.hashType == handshake_hash_combo) {
        rv = PK11_DigestOp(ss->ssl3.hs.md5, b, l);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
            return rv;
        }
        rv = PK11_DigestOp(ss->ssl3.hs.sha, b, l);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            return rv;
        }
    }
    return rv;
}

static SECStatus
ssl3_UpdateDefaultHandshakeHashes(sslSocket *ss, const unsigned char *b,
                                  unsigned int l)
{
    return ssl3_UpdateHandshakeHashesInt(ss, b, l,
                                         &ss->ssl3.hs.messages);
}

static SECStatus
ssl3_UpdateInnerHandshakeHashes(sslSocket *ss, const unsigned char *b,
                                unsigned int l)
{
    return ssl3_UpdateHandshakeHashesInt(ss, b, l,
                                         &ss->ssl3.hs.echInnerMessages);
}

/*
 * Handshake messages
 */
/* Called from  ssl3_InitHandshakeHashes()
**      ssl3_AppendHandshake()
**      ssl3_HandleV2ClientHello()
**      ssl3_HandleHandshakeMessage()
** Caller must hold the ssl3Handshake lock.
*/
SECStatus
ssl3_UpdateHandshakeHashes(sslSocket *ss, const unsigned char *b, unsigned int l)
{
    return ssl3_UpdateHandshakeHashesInt(ss, b, l, NULL);
}

SECStatus
ssl3_UpdatePostHandshakeHashes(sslSocket *ss, const unsigned char *b, unsigned int l)
{
    SECStatus rv = SECSuccess;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    PRINT_BUF(90, (ss, "post handshake hash input:", b, l));

    PORT_Assert(ss->ssl3.hs.hashType == handshake_hash_single);
    PORT_Assert(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
    rv = PK11_DigestOp(ss->ssl3.hs.shaPostHandshake, b, l);
    if (rv != SECSuccess) {
        PORT_SetError(SSL_ERROR_DIGEST_FAILURE);
    }
    return rv;
}

/* The next two functions serve to append the handshake header.
   The first one additionally writes to seqNumberBuffer
   the sequence number of the message we are generating.
   This function is used when generating the keyUpdate message in dtls13_enqueueKeyUpdateMessage.
*/
SECStatus
ssl3_AppendHandshakeHeaderAndStashSeqNum(sslSocket *ss, SSLHandshakeType t, PRUint32 length, PRUint64 *sendMessageSeqOut)
{
    PORT_Assert(t != ssl_hs_client_hello);
    SECStatus rv;

    /* If we already have a message in place, we need to enqueue it.
     * This empties the buffer. This is a convenient place to call
     * dtls_StageHandshakeMessage to mark the message boundary.
     */
    if (IS_DTLS(ss)) {
        rv = dtls_StageHandshakeMessage(ss);
        if (rv != SECSuccess) {
            return rv;
        }
    }

    SSL_TRC(30, ("%d: SSL3[%d]: append handshake header: type %s",
                 SSL_GETPID(), ss->fd, ssl3_DecodeHandshakeType(t)));

    rv = ssl3_AppendHandshakeNumber(ss, t, 1);
    if (rv != SECSuccess) {
        return rv; /* error code set by AppendHandshake, if applicable. */
    }
    rv = ssl3_AppendHandshakeNumber(ss, length, 3);
    if (rv != SECSuccess) {
        return rv; /* error code set by AppendHandshake, if applicable. */
    }

    if (IS_DTLS(ss)) {
        /* RFC 9147. 5.2.  DTLS Handshake Message Format.
         * In DTLS 1.3, the message transcript is computed over the original TLS
         * 1.3-style Handshake messages without the message_seq,
         * fragment_offset, and fragment_length values.  Note that this is a
         * change from DTLS 1.2 where those values were included in the transcript. */
        PRBool suppressHash = ss->version == SSL_LIBRARY_VERSION_TLS_1_3 ? PR_TRUE : PR_FALSE;

        /* Note that we make an unfragmented message here. We fragment in the
         * transmission code, if necessary */
        rv = ssl3_AppendHandshakeNumberSuppressHash(ss, ss->ssl3.hs.sendMessageSeq, 2, suppressHash);
        if (rv != SECSuccess) {
            return rv; /* error code set by AppendHandshake, if applicable. */
        }
        /* In case if we provide a buffer for the sequence message,
        we write down sendMessageSeq to the buffer. */
        if (sendMessageSeqOut != NULL) {
            *sendMessageSeqOut = ss->ssl3.hs.sendMessageSeq;
        }
        ss->ssl3.hs.sendMessageSeq++;

        /* 0 is the fragment offset, because it's not fragmented yet */
        rv = ssl3_AppendHandshakeNumberSuppressHash(ss, 0, 3, suppressHash);
        if (rv != SECSuccess) {
            return rv; /* error code set by AppendHandshake, if applicable. */
        }

        /* Fragment length -- set to the packet length because not fragmented */
        rv = ssl3_AppendHandshakeNumberSuppressHash(ss, length, 3, suppressHash);
        if (rv != SECSuccess) {
            return rv; /* error code set by AppendHandshake, if applicable. */
        }
    }

    return rv; /* error code set by AppendHandshake, if applicable. */
}

/* The function calls the ssl3_AppendHandshakeHeaderAndStashSeqNum implemented above.
   As in the majority of the cases we do not need the last parameter,
   we separate out this function. */
SECStatus
ssl3_AppendHandshakeHeader(sslSocket *ss, SSLHandshakeType t, PRUint32 length)
{
    return ssl3_AppendHandshakeHeaderAndStashSeqNum(ss, t, length, NULL);
}

/**************************************************************************
 * Consume Handshake functions.
 *
 * All data used in these functions is protected by two locks,
 * the RecvBufLock and the SSL3HandshakeLock
 **************************************************************************/

/* Read up the next "bytes" number of bytes from the (decrypted) input
 * stream "b" (which is *length bytes long). Copy them into buffer "v".
 * Reduces *length by bytes.  Advances *b by bytes.
 *
 * If this function returns SECFailure, it has already sent an alert,
 * and has set a generic error code.  The caller should probably
 * override the generic error code by setting another.
 */
SECStatus
ssl3_ConsumeHandshake(sslSocket *ss, void *v, PRUint32 bytes, PRUint8 **b,
                      PRUint32 *length)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if ((PRUint32)bytes > *length) {
        return ssl3_DecodeError(ss);
    }
    PORT_Memcpy(v, *b, bytes);
    PRINT_BUF(60, (ss, "consume bytes:", *b, bytes));
    *b += bytes;
    *length -= bytes;
    return SECSuccess;
}

/* Read up the next "bytes" number of bytes from the (decrypted) input
 * stream "b" (which is *length bytes long), and interpret them as an
 * integer in network byte order.  Sets *num to the received value.
 * Reduces *length by bytes.  Advances *b by bytes.
 *
 * On error, an alert has been sent, and a generic error code has been set.
 */
SECStatus
ssl3_ConsumeHandshakeNumber64(sslSocket *ss, PRUint64 *num, PRUint32 bytes,
                              PRUint8 **b, PRUint32 *length)
{
    PRUint8 *buf = *b;
    PRUint32 i;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    *num = 0;
    if (bytes > sizeof(*num)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (bytes > *length) {
        return ssl3_DecodeError(ss);
    }
    PRINT_BUF(60, (ss, "consume bytes:", *b, bytes));

    for (i = 0; i < bytes; i++) {
        *num = (*num << 8) + buf[i];
    }
    *b += bytes;
    *length -= bytes;
    return SECSuccess;
}

SECStatus
ssl3_ConsumeHandshakeNumber(sslSocket *ss, PRUint32 *num, PRUint32 bytes,
                            PRUint8 **b, PRUint32 *length)
{
    PRUint64 num64;
    SECStatus rv;

    PORT_Assert(bytes <= sizeof(*num));
    if (bytes > sizeof(*num)) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    rv = ssl3_ConsumeHandshakeNumber64(ss, &num64, bytes, b, length);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    *num = num64 & 0xffffffff;
    return SECSuccess;
}

/* Read in two values from the incoming decrypted byte stream "b", which is
 * *length bytes long.  The first value is a number whose size is "bytes"
 * bytes long.  The second value is a byte-string whose size is the value
 * of the first number received.  The latter byte-string, and its length,
 * is returned in the SECItem i.
 *
 * Returns SECFailure (-1) on failure.
 * On error, an alert has been sent, and a generic error code has been set.
 *
 * RADICAL CHANGE for NSS 3.11.  All callers of this function make copies
 * of the data returned in the SECItem *i, so making a copy of it here
 * is simply wasteful.  So, This function now just sets SECItem *i to
 * point to the values in the buffer **b.
 */
SECStatus
ssl3_ConsumeHandshakeVariable(sslSocket *ss, SECItem *i, PRUint32 bytes,
                              PRUint8 **b, PRUint32 *length)
{
    PRUint32 count;
    SECStatus rv;

    PORT_Assert(bytes <= 3);
    i->len = 0;
    i->data = NULL;
    i->type = siBuffer;
    rv = ssl3_ConsumeHandshakeNumber(ss, &count, bytes, b, length);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (count > 0) {
        if (count > *length) {
            return ssl3_DecodeError(ss);
        }
        i->data = *b;
        i->len = count;
        *b += count;
        *length -= count;
    }
    return SECSuccess;
}

/* ssl3_TLSHashAlgorithmToOID converts a TLS hash identifier into an OID value.
 * If the hash is not recognised, SEC_OID_UNKNOWN is returned.
 *
 * See https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1 */
SECOidTag
ssl3_HashTypeToOID(SSLHashType hashType)
{
    switch (hashType) {
        case ssl_hash_sha1:
            return SEC_OID_SHA1;
        case ssl_hash_sha256:
            return SEC_OID_SHA256;
        case ssl_hash_sha384:
            return SEC_OID_SHA384;
        case ssl_hash_sha512:
            return SEC_OID_SHA512;
        default:
            break;
    }
    return SEC_OID_UNKNOWN;
}

SECOidTag
ssl3_AuthTypeToOID(SSLAuthType authType)
{
    switch (authType) {
        case ssl_auth_rsa_sign:
            return SEC_OID_PKCS1_RSA_ENCRYPTION;
        case ssl_auth_rsa_pss:
            return SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
        case ssl_auth_ecdsa:
            return SEC_OID_ANSIX962_EC_PUBLIC_KEY;
        case ssl_auth_dsa:
            return SEC_OID_ANSIX9_DSA_SIGNATURE;
        default:
            break;
    }
    /* shouldn't ever get there */
    PORT_Assert(0);
    return SEC_OID_UNKNOWN;
}

SSLHashType
ssl_SignatureSchemeToHashType(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_rsa_pkcs1_sha1:
        case ssl_sig_dsa_sha1:
        case ssl_sig_ecdsa_sha1:
            return ssl_hash_sha1;
        case ssl_sig_rsa_pkcs1_sha256:
        case ssl_sig_ecdsa_secp256r1_sha256:
        case ssl_sig_rsa_pss_rsae_sha256:
        case ssl_sig_rsa_pss_pss_sha256:
        case ssl_sig_dsa_sha256:
            return ssl_hash_sha256;
        case ssl_sig_rsa_pkcs1_sha384:
        case ssl_sig_ecdsa_secp384r1_sha384:
        case ssl_sig_rsa_pss_rsae_sha384:
        case ssl_sig_rsa_pss_pss_sha384:
        case ssl_sig_dsa_sha384:
            return ssl_hash_sha384;
        case ssl_sig_rsa_pkcs1_sha512:
        case ssl_sig_ecdsa_secp521r1_sha512:
        case ssl_sig_rsa_pss_rsae_sha512:
        case ssl_sig_rsa_pss_pss_sha512:
        case ssl_sig_dsa_sha512:
            return ssl_hash_sha512;
        case ssl_sig_rsa_pkcs1_sha1md5:
            return ssl_hash_none; /* Special for TLS 1.0/1.1. */
        case ssl_sig_none:
        case ssl_sig_ed25519:
        case ssl_sig_ed448:
            break;
    }
    PORT_Assert(0);
    return ssl_hash_none;
}

static PRBool
ssl_SignatureSchemeMatchesSpkiOid(SSLSignatureScheme scheme, SECOidTag spkiOid)
{
    SECOidTag authOid = ssl3_AuthTypeToOID(ssl_SignatureSchemeToAuthType(scheme));

    if (spkiOid == authOid) {
        return PR_TRUE;
    }
    if ((authOid == SEC_OID_PKCS1_RSA_ENCRYPTION) &&
        (spkiOid == SEC_OID_X500_RSA_ENCRYPTION)) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

/* Validate that the signature scheme works for the given key type. */
PRBool
ssl_SignatureSchemeValid(SSLSignatureScheme scheme, SECOidTag spkiOid,
                         PRBool isTls13)
{
    if (!ssl_IsSupportedSignatureScheme(scheme)) {
        return PR_FALSE;
    }
    /* if we are purposefully passed SEC_OID_UNKNOWN, it means
     * we not checking the scheme against a potential key, so skip
     * the call */
    if ((spkiOid != SEC_OID_UNKNOWN) &&
        !ssl_SignatureSchemeMatchesSpkiOid(scheme, spkiOid)) {
        return PR_FALSE;
    }
    if (isTls13) {
        if (ssl_SignatureSchemeToHashType(scheme) == ssl_hash_sha1) {
            return PR_FALSE;
        }
        if (ssl_IsRsaPkcs1SignatureScheme(scheme)) {
            return PR_FALSE;
        }
        if (ssl_IsDsaSignatureScheme(scheme)) {
            return PR_FALSE;
        }
        /* With TLS 1.3, EC keys should have been selected based on calling
         * ssl_SignatureSchemeFromSpki(), reject them otherwise. */
        return spkiOid != SEC_OID_ANSIX962_EC_PUBLIC_KEY;
    }
    return PR_TRUE;
}

static SECStatus
ssl_SignatureSchemeFromPssSpki(const CERTSubjectPublicKeyInfo *spki,
                               SSLSignatureScheme *scheme)
{
    SECKEYRSAPSSParams pssParam = { 0 };
    PORTCheapArenaPool arena;
    SECStatus rv;

    /* The key doesn't have parameters, boo. */
    if (!spki->algorithm.parameters.len) {
        *scheme = ssl_sig_none;
        return SECSuccess;
    }

    PORT_InitCheapArena(&arena, DER_DEFAULT_CHUNKSIZE);
    rv = SEC_QuickDERDecodeItem(&arena.arena, &pssParam,
                                SEC_ASN1_GET(SECKEY_RSAPSSParamsTemplate),
                                &spki->algorithm.parameters);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* Not having hashAlg means SHA-1 and we don't accept that. */
    if (!pssParam.hashAlg) {
        goto loser;
    }
    switch (SECOID_GetAlgorithmTag(pssParam.hashAlg)) {
        case SEC_OID_SHA256:
            *scheme = ssl_sig_rsa_pss_pss_sha256;
            break;
        case SEC_OID_SHA384:
            *scheme = ssl_sig_rsa_pss_pss_sha384;
            break;
        case SEC_OID_SHA512:
            *scheme = ssl_sig_rsa_pss_pss_sha512;
            break;
        default:
            goto loser;
    }

    PORT_DestroyCheapArena(&arena);
    return SECSuccess;

loser:
    PORT_DestroyCheapArena(&arena);
    PORT_SetError(SSL_ERROR_BAD_CERTIFICATE);
    return SECFailure;
}

static SECStatus
ssl_SignatureSchemeFromEcSpki(const CERTSubjectPublicKeyInfo *spki,
                              SSLSignatureScheme *scheme)
{
    const sslNamedGroupDef *group;
    SECKEYPublicKey *key;

    key = SECKEY_ExtractPublicKey(spki);
    if (!key) {
        PORT_SetError(SSL_ERROR_BAD_CERTIFICATE);
        return SECFailure;
    }
    group = ssl_ECPubKey2NamedGroup(key);
    SECKEY_DestroyPublicKey(key);
    if (!group) {
        PORT_SetError(SSL_ERROR_BAD_CERTIFICATE);
        return SECFailure;
    }
    switch (group->name) {
        case ssl_grp_ec_secp256r1:
            *scheme = ssl_sig_ecdsa_secp256r1_sha256;
            return SECSuccess;
        case ssl_grp_ec_secp384r1:
            *scheme = ssl_sig_ecdsa_secp384r1_sha384;
            return SECSuccess;
        case ssl_grp_ec_secp521r1:
            *scheme = ssl_sig_ecdsa_secp521r1_sha512;
            return SECSuccess;
        default:
            break;
    }
    PORT_SetError(SSL_ERROR_BAD_CERTIFICATE);
    return SECFailure;
}

/* Newer signature schemes are designed so that a single SPKI can be used with
 * that scheme.  This determines that scheme from the SPKI. If the SPKI doesn't
 * have a single scheme, |*scheme| is set to ssl_sig_none. */
SECStatus
ssl_SignatureSchemeFromSpki(const CERTSubjectPublicKeyInfo *spki,
                            PRBool isTls13, SSLSignatureScheme *scheme)
{
    SECOidTag spkiOid = SECOID_GetAlgorithmTag(&spki->algorithm);

    if (spkiOid == SEC_OID_PKCS1_RSA_PSS_SIGNATURE) {
        return ssl_SignatureSchemeFromPssSpki(spki, scheme);
    }

    /* Only do this lookup for TLS 1.3, where the scheme can be determined from
     * the SPKI alone because the ECDSA key size determines the hash. Earlier
     * TLS versions allow the same EC key to be used with different hashes. */
    if (isTls13 && spkiOid == SEC_OID_ANSIX962_EC_PUBLIC_KEY) {
        return ssl_SignatureSchemeFromEcSpki(spki, scheme);
    }

    *scheme = ssl_sig_none;
    return SECSuccess;
}

/* Check that a signature scheme is enabled by configuration. */
PRBool
ssl_SignatureSchemeEnabled(const sslSocket *ss, SSLSignatureScheme scheme)
{
    unsigned int i;
    for (i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
        if (scheme == ss->ssl3.signatureSchemes[i]) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

static PRBool
ssl_SignatureKeyMatchesSpkiOid(const ssl3KEADef *keaDef, SECOidTag spkiOid)
{
    switch (spkiOid) {
        case SEC_OID_X500_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_ENCRYPTION:
        case SEC_OID_PKCS1_RSA_PSS_SIGNATURE:
            return keaDef->signKeyType == rsaKey;
        case SEC_OID_ANSIX9_DSA_SIGNATURE:
            return keaDef->signKeyType == dsaKey;
        case SEC_OID_ANSIX962_EC_PUBLIC_KEY:
            return keaDef->signKeyType == ecKey;
        default:
            break;
    }
    return PR_FALSE;
}

/* ssl3_CheckSignatureSchemeConsistency checks that the signature algorithm
 * identifier in |scheme| is consistent with the public key in |spki|. It also
 * checks the hash algorithm against the configured signature algorithms.  If
 * all the tests pass, SECSuccess is returned. Otherwise, PORT_SetError is
 * called and SECFailure is returned. */
SECStatus
ssl_CheckSignatureSchemeConsistency(sslSocket *ss, SSLSignatureScheme scheme,
                                    CERTSubjectPublicKeyInfo *spki)
{
    SSLSignatureScheme spkiScheme;
    PRBool isTLS13 = ss->version == SSL_LIBRARY_VERSION_TLS_1_3;
    SECOidTag spkiOid;
    SECStatus rv;

    rv = ssl_SignatureSchemeFromSpki(spki, isTLS13, &spkiScheme);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (spkiScheme != ssl_sig_none) {
        /* The SPKI in the certificate can only be used for a single scheme. */
        if (spkiScheme != scheme ||
            !ssl_SignatureSchemeEnabled(ss, scheme)) {
            PORT_SetError(SSL_ERROR_INCORRECT_SIGNATURE_ALGORITHM);
            return SECFailure;
        }
        return SECSuccess;
    }

    spkiOid = SECOID_GetAlgorithmTag(&spki->algorithm);

    /* If we're a client, check that the signature algorithm matches the signing
     * key type of the cipher suite. */
    if (!isTLS13 && !ss->sec.isServer) {
        if (!ssl_SignatureKeyMatchesSpkiOid(ss->ssl3.hs.kea_def, spkiOid)) {
            PORT_SetError(SSL_ERROR_INCORRECT_SIGNATURE_ALGORITHM);
            return SECFailure;
        }
    }

    /* Verify that the signature scheme matches the signing key. */
    if ((spkiOid == SEC_OID_UNKNOWN) ||
        !ssl_SignatureSchemeValid(scheme, spkiOid, isTLS13)) {
        PORT_SetError(SSL_ERROR_INCORRECT_SIGNATURE_ALGORITHM);
        return SECFailure;
    }

    if (!ssl_SignatureSchemeEnabled(ss, scheme)) {
        PORT_SetError(SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM);
        return SECFailure;
    }

    return SECSuccess;
}

PRBool
ssl_IsSupportedSignatureScheme(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_rsa_pkcs1_sha1:
        case ssl_sig_rsa_pkcs1_sha256:
        case ssl_sig_rsa_pkcs1_sha384:
        case ssl_sig_rsa_pkcs1_sha512:
        case ssl_sig_rsa_pss_rsae_sha256:
        case ssl_sig_rsa_pss_rsae_sha384:
        case ssl_sig_rsa_pss_rsae_sha512:
        case ssl_sig_rsa_pss_pss_sha256:
        case ssl_sig_rsa_pss_pss_sha384:
        case ssl_sig_rsa_pss_pss_sha512:
        case ssl_sig_ecdsa_secp256r1_sha256:
        case ssl_sig_ecdsa_secp384r1_sha384:
        case ssl_sig_ecdsa_secp521r1_sha512:
        case ssl_sig_dsa_sha1:
        case ssl_sig_dsa_sha256:
        case ssl_sig_dsa_sha384:
        case ssl_sig_dsa_sha512:
        case ssl_sig_ecdsa_sha1:
            return ssl_SchemePolicyOK(scheme, kSSLSigSchemePolicy);
            break;

        case ssl_sig_rsa_pkcs1_sha1md5:
        case ssl_sig_none:
        case ssl_sig_ed25519:
        case ssl_sig_ed448:
            return PR_FALSE;
    }
    return PR_FALSE;
}

PRBool
ssl_IsRsaPssSignatureScheme(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_rsa_pss_rsae_sha256:
        case ssl_sig_rsa_pss_rsae_sha384:
        case ssl_sig_rsa_pss_rsae_sha512:
        case ssl_sig_rsa_pss_pss_sha256:
        case ssl_sig_rsa_pss_pss_sha384:
        case ssl_sig_rsa_pss_pss_sha512:
            return PR_TRUE;

        default:
            return PR_FALSE;
    }
    return PR_FALSE;
}

PRBool
ssl_IsRsaeSignatureScheme(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_rsa_pss_rsae_sha256:
        case ssl_sig_rsa_pss_rsae_sha384:
        case ssl_sig_rsa_pss_rsae_sha512:
            return PR_TRUE;

        default:
            return PR_FALSE;
    }
    return PR_FALSE;
}

PRBool
ssl_IsRsaPkcs1SignatureScheme(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_rsa_pkcs1_sha256:
        case ssl_sig_rsa_pkcs1_sha384:
        case ssl_sig_rsa_pkcs1_sha512:
        case ssl_sig_rsa_pkcs1_sha1:
            return PR_TRUE;

        default:
            return PR_FALSE;
    }
    return PR_FALSE;
}

PRBool
ssl_IsDsaSignatureScheme(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_dsa_sha256:
        case ssl_sig_dsa_sha384:
        case ssl_sig_dsa_sha512:
        case ssl_sig_dsa_sha1:
            return PR_TRUE;

        default:
            return PR_FALSE;
    }
    return PR_FALSE;
}

SSLAuthType
ssl_SignatureSchemeToAuthType(SSLSignatureScheme scheme)
{
    switch (scheme) {
        case ssl_sig_rsa_pkcs1_sha1:
        case ssl_sig_rsa_pkcs1_sha1md5:
        case ssl_sig_rsa_pkcs1_sha256:
        case ssl_sig_rsa_pkcs1_sha384:
        case ssl_sig_rsa_pkcs1_sha512:
        /* We report based on the key type for PSS signatures. */
        case ssl_sig_rsa_pss_rsae_sha256:
        case ssl_sig_rsa_pss_rsae_sha384:
        case ssl_sig_rsa_pss_rsae_sha512:
            return ssl_auth_rsa_sign;
        case ssl_sig_rsa_pss_pss_sha256:
        case ssl_sig_rsa_pss_pss_sha384:
        case ssl_sig_rsa_pss_pss_sha512:
            return ssl_auth_rsa_pss;
        case ssl_sig_ecdsa_secp256r1_sha256:
        case ssl_sig_ecdsa_secp384r1_sha384:
        case ssl_sig_ecdsa_secp521r1_sha512:
        case ssl_sig_ecdsa_sha1:
            return ssl_auth_ecdsa;
        case ssl_sig_dsa_sha1:
        case ssl_sig_dsa_sha256:
        case ssl_sig_dsa_sha384:
        case ssl_sig_dsa_sha512:
            return ssl_auth_dsa;

        default:
            PORT_Assert(0);
    }
    return ssl_auth_null;
}

/* ssl_ConsumeSignatureScheme reads a SSLSignatureScheme (formerly
 * SignatureAndHashAlgorithm) structure from |b| and puts the resulting value
 * into |out|. |b| and |length| are updated accordingly.
 *
 * See https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1 */
SECStatus
ssl_ConsumeSignatureScheme(sslSocket *ss, PRUint8 **b,
                           PRUint32 *length, SSLSignatureScheme *out)
{
    PRUint32 tmp;
    SECStatus rv;

    rv = ssl3_ConsumeHandshakeNumber(ss, &tmp, 2, b, length);
    if (rv != SECSuccess) {
        return SECFailure; /* Alert sent, Error code set already. */
    }
    if (!ssl_IsSupportedSignatureScheme((SSLSignatureScheme)tmp)) {
        SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
        PORT_SetError(SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM);
        return SECFailure;
    }
    *out = (SSLSignatureScheme)tmp;
    return SECSuccess;
}

/**************************************************************************
 * end of Consume Handshake functions.
 **************************************************************************/

static SECStatus
ssl3_ComputeHandshakeHash(unsigned char *buf, unsigned int len,
                          SSLHashType hashAlg, SSL3Hashes *hashes)
{
    SECStatus rv = SECFailure;
    PK11Context *hashContext = PK11_CreateDigestContext(
        ssl3_HashTypeToOID(hashAlg));

    if (!hashContext) {
        return rv;
    }
    rv = PK11_DigestBegin(hashContext);
    if (rv == SECSuccess) {
        rv = PK11_DigestOp(hashContext, buf, len);
    }
    if (rv == SECSuccess) {
        rv = PK11_DigestFinal(hashContext, hashes->u.raw, &hashes->len,
                              sizeof(hashes->u.raw));
    }
    if (rv == SECSuccess) {
        hashes->hashAlg = hashAlg;
    }
    PK11_DestroyContext(hashContext, PR_TRUE);
    return rv;
}

/* Extract the hashes of handshake messages to this point.
 * Called from ssl3_SendCertificateVerify
 *             ssl3_SendFinished
 *             ssl3_HandleHandshakeMessage
 *
 * Caller must hold the SSL3HandshakeLock.
 * Caller must hold a read or write lock on the Spec R/W lock.
 *  (There is presently no way to assert on a Read lock.)
 */
SECStatus
ssl3_ComputeHandshakeHashes(sslSocket *ss,
                            ssl3CipherSpec *spec, /* uses ->master_secret */
                            SSL3Hashes *hashes,   /* output goes here. */
                            PRUint32 sender)
{
    SECStatus rv = SECSuccess;
    PRBool isTLS = (PRBool)(spec->version > SSL_LIBRARY_VERSION_3_0);
    unsigned int outLength;
    PRUint8 md5_inner[MAX_MAC_LENGTH];
    PRUint8 sha_inner[MAX_MAC_LENGTH];

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    if (ss->ssl3.hs.hashType == handshake_hash_unknown) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    hashes->hashAlg = ssl_hash_none;

    if (ss->ssl3.hs.hashType == handshake_hash_single) {
        PK11Context *h;
        unsigned int stateLen;
        unsigned char stackBuf[1024];
        unsigned char *stateBuf = NULL;

        h = ss->ssl3.hs.sha;
        stateBuf = PK11_SaveContextAlloc(h, stackBuf,
                                         sizeof(stackBuf), &stateLen);
        if (stateBuf == NULL) {
            ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
            rv = SECFailure;
            goto tls12_loser;
        }
        rv |= PK11_DigestFinal(h, hashes->u.raw, &hashes->len,
                               sizeof(hashes->u.raw));
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
            rv = SECFailure;
            goto tls12_loser;
        }

        hashes->hashAlg = ssl3_GetSuitePrfHash(ss);

    tls12_loser:
        if (stateBuf) {
            if (PK11_RestoreContext(h, stateBuf, stateLen) != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
                rv = SECFailure;
            }
            if (stateBuf != stackBuf) {
                PORT_ZFree(stateBuf, stateLen);
            }
        }
    } else if (ss->ssl3.hs.hashType == handshake_hash_record) {
        rv = ssl3_ComputeHandshakeHash(ss->ssl3.hs.messages.buf,
                                       ss->ssl3.hs.messages.len,
                                       ssl3_GetSuitePrfHash(ss),
                                       hashes);
    } else {
        PK11Context *md5;
        PK11Context *sha = NULL;
        unsigned char *md5StateBuf = NULL;
        unsigned char *shaStateBuf = NULL;
        unsigned int md5StateLen, shaStateLen;
        unsigned char md5StackBuf[256];
        unsigned char shaStackBuf[512];
        const int md5Pad = ssl_GetMacDefByAlg(ssl_mac_md5)->pad_size;
        const int shaPad = ssl_GetMacDefByAlg(ssl_mac_sha)->pad_size;

        md5StateBuf = PK11_SaveContextAlloc(ss->ssl3.hs.md5, md5StackBuf,
                                            sizeof md5StackBuf, &md5StateLen);
        if (md5StateBuf == NULL) {
            ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
            rv = SECFailure;
            goto loser;
        }
        md5 = ss->ssl3.hs.md5;

        shaStateBuf = PK11_SaveContextAlloc(ss->ssl3.hs.sha, shaStackBuf,
                                            sizeof shaStackBuf, &shaStateLen);
        if (shaStateBuf == NULL) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            rv = SECFailure;
            goto loser;
        }
        sha = ss->ssl3.hs.sha;

        if (!isTLS) {
            /* compute hashes for SSL3. */
            unsigned char s[4];

            if (!spec->masterSecret) {
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HANDSHAKE);
                rv = SECFailure;
                goto loser;
            }

            s[0] = (unsigned char)(sender >> 24);
            s[1] = (unsigned char)(sender >> 16);
            s[2] = (unsigned char)(sender >> 8);
            s[3] = (unsigned char)sender;

            if (sender != 0) {
                rv |= PK11_DigestOp(md5, s, 4);
                PRINT_BUF(95, (NULL, "MD5 inner: sender", s, 4));
            }

            PRINT_BUF(95, (NULL, "MD5 inner: MAC Pad 1", mac_pad_1, md5Pad));

            rv |= PK11_DigestKey(md5, spec->masterSecret);
            rv |= PK11_DigestOp(md5, mac_pad_1, md5Pad);
            rv |= PK11_DigestFinal(md5, md5_inner, &outLength, MD5_LENGTH);
            PORT_Assert(rv != SECSuccess || outLength == MD5_LENGTH);
            if (rv != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
                rv = SECFailure;
                goto loser;
            }

            PRINT_BUF(95, (NULL, "MD5 inner: result", md5_inner, outLength));

            if (sender != 0) {
                rv |= PK11_DigestOp(sha, s, 4);
                PRINT_BUF(95, (NULL, "SHA inner: sender", s, 4));
            }

            PRINT_BUF(95, (NULL, "SHA inner: MAC Pad 1", mac_pad_1, shaPad));

            rv |= PK11_DigestKey(sha, spec->masterSecret);
            rv |= PK11_DigestOp(sha, mac_pad_1, shaPad);
            rv |= PK11_DigestFinal(sha, sha_inner, &outLength, SHA1_LENGTH);
            PORT_Assert(rv != SECSuccess || outLength == SHA1_LENGTH);
            if (rv != SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
                rv = SECFailure;
                goto loser;
            }

            PRINT_BUF(95, (NULL, "SHA inner: result", sha_inner, outLength));

            PRINT_BUF(95, (NULL, "MD5 outer: MAC Pad 2", mac_pad_2, md5Pad));
            PRINT_BUF(95, (NULL, "MD5 outer: MD5 inner", md5_inner, MD5_LENGTH));

            rv |= PK11_DigestBegin(md5);
            rv |= PK11_DigestKey(md5, spec->masterSecret);
            rv |= PK11_DigestOp(md5, mac_pad_2, md5Pad);
            rv |= PK11_DigestOp(md5, md5_inner, MD5_LENGTH);
        }
        rv |= PK11_DigestFinal(md5, hashes->u.s.md5, &outLength, MD5_LENGTH);
        PORT_Assert(rv != SECSuccess || outLength == MD5_LENGTH);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
            rv = SECFailure;
            goto loser;
        }

        PRINT_BUF(60, (NULL, "MD5 outer: result", hashes->u.s.md5, MD5_LENGTH));

        if (!isTLS) {
            PRINT_BUF(95, (NULL, "SHA outer: MAC Pad 2", mac_pad_2, shaPad));
            PRINT_BUF(95, (NULL, "SHA outer: SHA inner", sha_inner, SHA1_LENGTH));

            rv |= PK11_DigestBegin(sha);
            rv |= PK11_DigestKey(sha, spec->masterSecret);
            rv |= PK11_DigestOp(sha, mac_pad_2, shaPad);
            rv |= PK11_DigestOp(sha, sha_inner, SHA1_LENGTH);
        }
        rv |= PK11_DigestFinal(sha, hashes->u.s.sha, &outLength, SHA1_LENGTH);
        PORT_Assert(rv != SECSuccess || outLength == SHA1_LENGTH);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
            rv = SECFailure;
            goto loser;
        }

        PRINT_BUF(60, (NULL, "SHA outer: result", hashes->u.s.sha, SHA1_LENGTH));

        hashes->len = MD5_LENGTH + SHA1_LENGTH;

    loser:
        if (md5StateBuf) {
            if (PK11_RestoreContext(ss->ssl3.hs.md5, md5StateBuf, md5StateLen) !=
                SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_MD5_DIGEST_FAILURE);
                rv = SECFailure;
            }
            if (md5StateBuf != md5StackBuf) {
                PORT_ZFree(md5StateBuf, md5StateLen);
            }
        }
        if (shaStateBuf) {
            if (PK11_RestoreContext(ss->ssl3.hs.sha, shaStateBuf, shaStateLen) !=
                SECSuccess) {
                ssl_MapLowLevelError(SSL_ERROR_SHA_DIGEST_FAILURE);
                rv = SECFailure;
            }
            if (shaStateBuf != shaStackBuf) {
                PORT_ZFree(shaStateBuf, shaStateLen);
            }
        }
    }
    return rv;
}

/**************************************************************************
 * end of Handshake Hash functions.
 * Begin Send and Handle functions for handshakes.
 **************************************************************************/

#ifdef TRACE
#define CHTYPE(t)          \
    case client_hello_##t: \
        return #t;

static const char *
ssl_ClientHelloTypeName(sslClientHelloType type)
{
    switch (type) {
        CHTYPE(initial);
        CHTYPE(retry);
        CHTYPE(retransmit);    /* DTLS only */
        CHTYPE(renegotiation); /* TLS <= 1.2 only */
    }
    PORT_Assert(0);
    return NULL;
}
#undef CHTYPE
#endif

PR_STATIC_ASSERT(SSL3_SESSIONID_BYTES == SSL3_RANDOM_LENGTH);
static void
ssl_MakeFakeSid(sslSocket *ss, PRUint8 *buf)
{
    PRUint8 x = 0x5a;
    int i;
    for (i = 0; i < SSL3_SESSIONID_BYTES; ++i) {
        x += ss->ssl3.hs.client_random[i];
        buf[i] = x;
    }
}

/* Set the version fields of the cipher spec for a ClientHello. */
static void
ssl_SetClientHelloSpecVersion(sslSocket *ss, ssl3CipherSpec *spec)
{
    ssl_GetSpecWriteLock(ss);
    PORT_Assert(spec->cipherDef->cipher == cipher_null);
    /* This is - a best guess - but it doesn't matter here. */
    spec->version = ss->vrange.max;
    if (IS_DTLS(ss)) {
        spec->recordVersion = SSL_LIBRARY_VERSION_DTLS_1_0_WIRE;
    } else {
        /* For new connections, cap the record layer version number of TLS
         * ClientHello to { 3, 1 } (TLS 1.0). Some TLS 1.0 servers (which seem
         * to use F5 BIG-IP) ignore ClientHello.client_version and use the
         * record layer version number (TLSPlaintext.version) instead when
         * negotiating protocol versions. In addition, if the record layer
         * version number of ClientHello is { 3, 2 } (TLS 1.1) or higher, these
         * servers reset the TCP connections. Lastly, some F5 BIG-IP servers
         * hang if a record containing a ClientHello has a version greater than
         * { 3, 1 } and a length greater than 255. Set this flag to work around
         * such servers.
         *
         * The final version is set when a version is negotiated.
         */
        spec->recordVersion = PR_MIN(SSL_LIBRARY_VERSION_TLS_1_0,
                                     ss->vrange.max);
    }
    ssl_ReleaseSpecWriteLock(ss);
}

SECStatus
ssl3_InsertChHeaderSize(const sslSocket *ss, sslBuffer *preamble, const sslBuffer *extensions)
{
    SECStatus rv;
    unsigned int msgLen = preamble->len;
    msgLen += extensions->len ? (2 + extensions->len) : 0;
    unsigned int headerLen = IS_DTLS(ss) ? 12 : 4;

    /* Record the message length. */
    rv = sslBuffer_InsertNumber(preamble, 1, msgLen - headerLen, 3);
    if (rv != SECSuccess) {
        return SECFailure; /* code set */
    }
    if (IS_DTLS(ss)) {
        /* Record the (unfragmented) fragment length. */
        unsigned int offset = 1 /* ch */ + 3 /* len */ +
                              2 /* seq */ + 3 /* fragment offset */;
        rv = sslBuffer_InsertNumber(preamble, offset, msgLen - headerLen, 3);
        if (rv != SECSuccess) {
            return SECFailure; /* code set */
        }
    }

    return SECSuccess;
}

static SECStatus
ssl3_AppendCipherSuites(sslSocket *ss, PRBool fallbackSCSV, sslBuffer *buf)
{
    SECStatus rv;
    unsigned int offset;
    unsigned int i;
    unsigned int saveLen;

    rv = sslBuffer_Skip(buf, 2, &offset);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->ssl3.hs.sendingSCSV) {
        /* Add the actual SCSV */
        rv = sslBuffer_AppendNumber(buf, TLS_EMPTY_RENEGOTIATION_INFO_SCSV,
                                    sizeof(ssl3CipherSuite));
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }
    if (fallbackSCSV) {
        rv = sslBuffer_AppendNumber(buf, TLS_FALLBACK_SCSV,
                                    sizeof(ssl3CipherSuite));
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    saveLen = SSL_BUFFER_LEN(buf);
    /* CipherSuites are appended to Hello message here */
    for (i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        ssl3CipherSuiteCfg *suite = &ss->cipherSuites[i];
        if (ssl3_config_match(suite, ss->ssl3.policy, &ss->vrange, ss)) {
            rv = sslBuffer_AppendNumber(buf, suite->cipher_suite,
                                        sizeof(ssl3CipherSuite));
            if (rv != SECSuccess) {
                return SECFailure;
            }
        }
    }

    /* GREASE CipherSuites:
     * A client MAY select one or more GREASE cipher suite values and advertise
     * them in the "cipher_suites" field [RFC8701, Section 3.1]. */
    if (ss->opt.enableGrease && ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3) {
        rv = sslBuffer_AppendNumber(buf, ss->ssl3.hs.grease->idx[grease_cipher],
                                    sizeof(ssl3CipherSuite));
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    if (SSL_ALL_VERSIONS_DISABLED(&ss->vrange) ||
        (SSL_BUFFER_LEN(buf) - saveLen) == 0) {
        PORT_SetError(SSL_ERROR_SSL_DISABLED);
        return SECFailure;
    }

    return sslBuffer_InsertLength(buf, offset, 2);
}

SECStatus
ssl3_CreateClientHelloPreamble(sslSocket *ss, const sslSessionID *sid,
                               PRBool realSid, PRUint16 version, PRBool isEchInner,
                               const sslBuffer *extensions, sslBuffer *preamble)
{
    SECStatus rv;
    sslBuffer constructed = SSL_BUFFER_EMPTY;
    const PRUint8 *client_random = isEchInner ? ss->ssl3.hs.client_inner_random : ss->ssl3.hs.client_random;
    PORT_Assert(sid);
    PRBool fallbackSCSV = ss->opt.enableFallbackSCSV && !isEchInner &&
                          (!realSid || version < sid->version);

    rv = sslBuffer_AppendNumber(&constructed, ssl_hs_client_hello, 1);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = sslBuffer_Skip(&constructed, 3, NULL);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (IS_DTLS(ss)) {
        /* Note that we make an unfragmented message here. We fragment in the
         * transmission code, if necessary */
        rv = sslBuffer_AppendNumber(&constructed, ss->ssl3.hs.sendMessageSeq, 2);
        if (rv != SECSuccess) {
            goto loser;
        }
        ss->ssl3.hs.sendMessageSeq++;

        /* 0 is the fragment offset, because it's not fragmented yet */
        rv = sslBuffer_AppendNumber(&constructed, 0, 3);
        if (rv != SECSuccess) {
            goto loser;
        }

        /* Fragment length -- set to the packet length because not fragmented */
        rv = sslBuffer_Skip(&constructed, 3, NULL);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    if (ss->firstHsDone) {
        /* The client hello version must stay unchanged to work around
         * the Windows SChannel bug described in ssl3_SendClientHello. */
        PORT_Assert(version == ss->clientHelloVersion);
    }

    ss->clientHelloVersion = PR_MIN(version, SSL_LIBRARY_VERSION_TLS_1_2);
    if (IS_DTLS(ss)) {
        PRUint16 dtlsVersion = dtls_TLSVersionToDTLSVersion(ss->clientHelloVersion);
        rv = sslBuffer_AppendNumber(&constructed, dtlsVersion, 2);
    } else {
        rv = sslBuffer_AppendNumber(&constructed, ss->clientHelloVersion, 2);
    }
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = sslBuffer_Append(&constructed, client_random, SSL3_RANDOM_LENGTH);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (sid->version < SSL_LIBRARY_VERSION_TLS_1_3 && !isEchInner) {
        rv = sslBuffer_AppendVariable(&constructed, sid->u.ssl3.sessionID,
                                      sid->u.ssl3.sessionIDLength, 1);
    } else if (ss->opt.enableTls13CompatMode && !IS_DTLS(ss)) {
        /* We're faking session resumption, so rather than create new
         * randomness, just mix up the client random a little. */
        PRUint8 buf[SSL3_SESSIONID_BYTES];
        ssl_MakeFakeSid(ss, buf);
        rv = sslBuffer_AppendVariable(&constructed, buf, SSL3_SESSIONID_BYTES, 1);
    } else {
        rv = sslBuffer_AppendNumber(&constructed, 0, 1);
    }
    if (rv != SECSuccess) {
        goto loser;
    }

    if (IS_DTLS(ss)) {
        /* This cookieLen applies to the cookie that appears in the DTLS
         * ClientHello, which isn't used in DTLS 1.3. */
        rv = sslBuffer_AppendVariable(&constructed, ss->ssl3.hs.cookie.data,
                                      ss->ssl3.hs.helloRetry ? 0 : ss->ssl3.hs.cookie.len,
                                      1);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    rv = ssl3_AppendCipherSuites(ss, fallbackSCSV, &constructed);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* Compression methods: count is always 1, null compression. */
    rv = sslBuffer_AppendNumber(&constructed, 1, 1);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = sslBuffer_AppendNumber(&constructed, ssl_compression_null, 1);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = ssl3_InsertChHeaderSize(ss, &constructed, extensions);
    if (rv != SECSuccess) {
        goto loser;
    }

    *preamble = constructed;
    return SECSuccess;
loser:
    sslBuffer_Clear(&constructed);
    return SECFailure;
}

/* Called from ssl3_HandleHelloRequest(),
 *             ssl3_RedoHandshake()
 *             ssl_BeginClientHandshake (when resuming ssl3 session)
 *             dtls_HandleHelloVerifyRequest(with resending=PR_TRUE)
 *
 * The |type| argument indicates what is going on here:
 * - client_hello_initial is set for the very first ClientHello
 * - client_hello_retry indicates that this is a second attempt after receiving
 *   a HelloRetryRequest (in TLS 1.3)
 * - client_hello_retransmit is used in DTLS when resending
 * - client_hello_renegotiation is used to renegotiate (in TLS <1.3)
 */
SECStatus
ssl3_SendClientHello(sslSocket *ss, sslClientHelloType type)
{
    sslSessionID *sid;
    SECStatus rv;
    PRBool isTLS = PR_FALSE;
    PRBool requestingResume = PR_FALSE;
    PRBool unlockNeeded = PR_FALSE;
    sslBuffer extensionBuf = SSL_BUFFER_EMPTY;
    PRUint16 version = ss->vrange.max;
    PRInt32 flags;
    sslBuffer chBuf = SSL_BUFFER_EMPTY;

    SSL_TRC(3, ("%d: SSL3[%d]: send %s ClientHello handshake", SSL_GETPID(),
                ss->fd, ssl_ClientHelloTypeName(type)));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    /* shouldn't get here if SSL3 is disabled, but ... */
    if (SSL_ALL_VERSIONS_DISABLED(&ss->vrange)) {
        PR_NOT_REACHED("No versions of SSL 3.0 or later are enabled");
        PORT_SetError(SSL_ERROR_SSL_DISABLED);
        return SECFailure;
    }

    /* If we are responding to a HelloRetryRequest, don't reinitialize. We need
     * to maintain the handshake hashes. */
    if (!ss->ssl3.hs.helloRetry) {
        ssl3_RestartHandshakeHashes(ss);
    }
    PORT_Assert(!ss->ssl3.hs.helloRetry || type == client_hello_retry);

    if (type == client_hello_initial) {
        ssl_SetClientHelloSpecVersion(ss, ss->ssl3.cwSpec);
    }
    /* These must be reset every handshake. */
    ssl3_ResetExtensionData(&ss->xtnData, ss);
    ss->ssl3.hs.sendingSCSV = PR_FALSE;
    ss->ssl3.hs.preliminaryInfo = 0;
    PORT_Assert(IS_DTLS(ss) || type != client_hello_retransmit);
    SECITEM_FreeItem(&ss->ssl3.hs.newSessionTicket.ticket, PR_FALSE);
    ss->ssl3.hs.receivedNewSessionTicket = PR_FALSE;

    /* How many suites does our PKCS11 support (regardless of policy)? */
    if (ssl3_config_match_init(ss) == 0) {
        return SECFailure; /* ssl3_config_match_init has set error code. */
    }

    /*
     * During a renegotiation, ss->clientHelloVersion will be used again to
     * work around a Windows SChannel bug. Ensure that it is still enabled.
     */
    if (ss->firstHsDone) {
        PORT_Assert(type != client_hello_initial);
        if (SSL_ALL_VERSIONS_DISABLED(&ss->vrange)) {
            PORT_SetError(SSL_ERROR_SSL_DISABLED);
            return SECFailure;
        }

        if (ss->clientHelloVersion < ss->vrange.min ||
            ss->clientHelloVersion > ss->vrange.max) {
            PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
            return SECFailure;
        }
    }

    /* Check if we have a ss->sec.ci.sid.
     * Check that it's not expired.
     * If we have an sid and it comes from an external cache, we use it. */
    if (ss->sec.ci.sid && ss->sec.ci.sid->cached == in_external_cache) {
        PORT_Assert(!ss->sec.isServer);
        sid = ssl_ReferenceSID(ss->sec.ci.sid);
        SSL_TRC(3, ("%d: SSL3[%d]: using external resumption token in ClientHello",
                    SSL_GETPID(), ss->fd));
    } else if (ss->sec.ci.sid && ss->statelessResume && type == client_hello_retry) {
        /* If we are sending a second ClientHello, reuse the same SID
         * as the original one. */
        sid = ssl_ReferenceSID(ss->sec.ci.sid);
    } else if (!ss->opt.noCache) {
        /* We ignore ss->sec.ci.sid here, and use ssl_Lookup because Lookup
         * handles expired entries and other details.
         * XXX If we've been called from ssl_BeginClientHandshake, then
         * this lookup is duplicative and wasteful.
         */
        sid = ssl_LookupSID(ssl_Time(ss), &ss->sec.ci.peer,
                            ss->sec.ci.port, ss->peerID, ss->url);
    } else {
        sid = NULL;
    }

    /* We can't resume based on a different token. If the sid exists,
     * make sure the token that holds the master secret still exists ...
     * If we previously did client-auth, make sure that the token that holds
     * the private key still exists, is logged in, hasn't been removed, etc.
     */
    if (sid) {
        PRBool sidOK = PR_TRUE;

        if (sid->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
            if (!tls13_ResumptionCompatible(ss, sid->u.ssl3.cipherSuite)) {
                sidOK = PR_FALSE;
            }
        } else {
            /* Check that the cipher suite we need is enabled. */
            const ssl3CipherSuiteCfg *suite =
                ssl_LookupCipherSuiteCfg(sid->u.ssl3.cipherSuite,
                                         ss->cipherSuites);
            SSLVersionRange vrange = { sid->version, sid->version };
            if (!suite || !ssl3_config_match(suite, ss->ssl3.policy, &vrange, ss)) {
                sidOK = PR_FALSE;
            }

            /* Check that no (valid) ECHConfigs are setup in combination with a
             * (resumable) TLS < 1.3 session id. */
            if (!PR_CLIST_IS_EMPTY(&ss->echConfigs)) {
                /* If there are ECH configs, the client must not resume but
                 * offer ECH. */
                sidOK = PR_FALSE;
            }
        }

        /* Check that we can recover the master secret. */
        if (sidOK) {
            PK11SlotInfo *slot = NULL;
            if (sid->u.ssl3.masterValid) {
                slot = SECMOD_LookupSlot(sid->u.ssl3.masterModuleID,
                                         sid->u.ssl3.masterSlotID);
            }
            if (slot == NULL) {
                sidOK = PR_FALSE;
            } else {
                PK11SymKey *wrapKey = NULL;
                if (!PK11_IsPresent(slot) ||
                    ((wrapKey = PK11_GetWrapKey(slot,
                                                sid->u.ssl3.masterWrapIndex,
                                                sid->u.ssl3.masterWrapMech,
                                                sid->u.ssl3.masterWrapSeries,
                                                ss->pkcs11PinArg)) == NULL)) {
                    sidOK = PR_FALSE;
                }
                if (wrapKey)
                    PK11_FreeSymKey(wrapKey);
                PK11_FreeSlot(slot);
                slot = NULL;
            }
        }
        /* If we previously did client-auth, make sure that the token that
        ** holds the private key still exists, is logged in, hasn't been
        ** removed, etc.
        */
        if (sidOK && !ssl3_ClientAuthTokenPresent(sid)) {
            sidOK = PR_FALSE;
        }

        if (sidOK) {
            /* Set version based on the sid. */
            if (ss->firstHsDone) {
                /*
                 * Windows SChannel compares the client_version inside the RSA
                 * EncryptedPreMasterSecret of a renegotiation with the
                 * client_version of the initial ClientHello rather than the
                 * ClientHello in the renegotiation. To work around this bug, we
                 * continue to use the client_version used in the initial
                 * ClientHello when renegotiating.
                 *
                 * The client_version of the initial ClientHello is still
                 * available in ss->clientHelloVersion. Ensure that
                 * sid->version is bounded within
                 * [ss->vrange.min, ss->clientHelloVersion], otherwise we
                 * can't use sid.
                 */
                if (sid->version >= ss->vrange.min &&
                    sid->version <= ss->clientHelloVersion) {
                    version = ss->clientHelloVersion;
                } else {
                    sidOK = PR_FALSE;
                }
            } else {
                /*
                 * Check sid->version is OK first.
                 * Previously, we would cap the version based on sid->version,
                 * but that prevents negotiation of a higher version if the
                 * previous session was reduced (e.g., with version fallback)
                 */
                if (sid->version < ss->vrange.min ||
                    sid->version > ss->vrange.max) {
                    sidOK = PR_FALSE;
                }
            }
        }

        if (!sidOK) {
            SSL_AtomicIncrementLong(&ssl3stats.sch_sid_cache_not_ok);
            ssl_UncacheSessionID(ss);
            ssl_FreeSID(sid);
            sid = NULL;
        }
    }

    if (sid) {
        requestingResume = PR_TRUE;
        SSL_AtomicIncrementLong(&ssl3stats.sch_sid_cache_hits);

        PRINT_BUF(4, (ss, "client, found session-id:", sid->u.ssl3.sessionID,
                      sid->u.ssl3.sessionIDLength));

        ss->ssl3.policy = sid->u.ssl3.policy;
    } else {
        SSL_AtomicIncrementLong(&ssl3stats.sch_sid_cache_misses);

        /*
         * Windows SChannel compares the client_version inside the RSA
         * EncryptedPreMasterSecret of a renegotiation with the
         * client_version of the initial ClientHello rather than the
         * ClientHello in the renegotiation. To work around this bug, we
         * continue to use the client_version used in the initial
         * ClientHello when renegotiating.
         */
        if (ss->firstHsDone) {
            version = ss->clientHelloVersion;
        }

        sid = ssl3_NewSessionID(ss, PR_FALSE);
        if (!sid) {
            return SECFailure; /* memory error is set */
        }
        /* ss->version isn't set yet, but the sid needs a sane value. */
        sid->version = version;
    }

    isTLS = (version > SSL_LIBRARY_VERSION_3_0);
    ssl_GetSpecWriteLock(ss);
    if (ss->ssl3.cwSpec->macDef->mac == ssl_mac_null) {
        /* SSL records are not being MACed. */
        ss->ssl3.cwSpec->version = version;
    }
    ssl_ReleaseSpecWriteLock(ss);

    ssl_FreeSID(ss->sec.ci.sid); /* release the old sid */
    ss->sec.ci.sid = sid;

    /* HACK for SCSV in SSL 3.0.  On initial handshake, prepend SCSV,
     * only if TLS is disabled.
     */
    if (!ss->firstHsDone && !isTLS) {
        /* Must set this before calling Hello Extension Senders,
         * to suppress sending of empty RI extension.
         */
        ss->ssl3.hs.sendingSCSV = PR_TRUE;
    }

    /* When we attempt session resumption (only), we must lock the sid to
     * prevent races with other resumption connections that receive a
     * NewSessionTicket that will cause the ticket in the sid to be replaced.
     * Once we've copied the session ticket into our ClientHello message, it
     * is OK for the ticket to change, so we just need to make sure we hold
     * the lock across the calls to ssl_ConstructExtensions.
     */
    if (sid->u.ssl3.lock) {
        unlockNeeded = PR_TRUE;
        PR_RWLock_Rlock(sid->u.ssl3.lock);
    }

    /* Generate a new random if this is the first attempt or renegotiation. */
    if (type == client_hello_initial ||
        type == client_hello_renegotiation) {
        rv = ssl3_GetNewRandom(ss->ssl3.hs.client_random);
        if (rv != SECSuccess) {
            goto loser; /* err set by GetNewRandom. */
        }
    }

    if (ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3) {
        rv = tls13_SetupClientHello(ss, type);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    /* Setup TLS ClientHello Extension Permutation? */
    if (type == client_hello_initial &&
        ss->vrange.max > SSL_LIBRARY_VERSION_3_0 &&
        ss->opt.enableChXtnPermutation) {
        rv = tls_ClientHelloExtensionPermutationSetup(ss);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    if (isTLS || (ss->firstHsDone && ss->peerRequestedProtection)) {
        rv = ssl_ConstructExtensions(ss, &extensionBuf, ssl_hs_client_hello);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    if (IS_DTLS(ss)) {
        ssl3_DisableNonDTLSSuites(ss);
    }

    rv = ssl3_CreateClientHelloPreamble(ss, sid, requestingResume, version,
                                        PR_FALSE, &extensionBuf, &chBuf);
    if (rv != SECSuccess) {
        goto loser; /* err set by ssl3_CreateClientHelloPreamble. */
    }

    if (!ss->ssl3.hs.echHpkeCtx) {
        if (extensionBuf.len) {
            rv = tls13_MaybeGreaseEch(ss, &chBuf, &extensionBuf);
            if (rv != SECSuccess) {
                goto loser; /* err set by tls13_MaybeGreaseEch. */
            }
            rv = ssl_InsertPaddingExtension(ss, chBuf.len, &extensionBuf);
            if (rv != SECSuccess) {
                goto loser; /* err set by ssl_InsertPaddingExtension. */
            }

            rv = ssl3_InsertChHeaderSize(ss, &chBuf, &extensionBuf);
            if (rv != SECSuccess) {
                goto loser; /* err set by ssl3_InsertChHeaderSize. */
            }

            /* If we are sending a PSK binder, replace the dummy value. */
            if (ssl3_ExtensionAdvertised(ss, ssl_tls13_pre_shared_key_xtn)) {
                rv = tls13_WriteExtensionsWithBinder(ss, &extensionBuf, &chBuf);
            } else {
                rv = sslBuffer_AppendNumber(&chBuf, extensionBuf.len, 2);
                if (rv != SECSuccess) {
                    goto loser;
                }
                rv = sslBuffer_AppendBuffer(&chBuf, &extensionBuf);
            }
            if (rv != SECSuccess) {
                goto loser; /* err set by sslBuffer_Append*. */
            }
        }

        /* If we already have a message in place, we need to enqueue it.
         * This empties the buffer. This is a convenient place to call
         * dtls_StageHandshakeMessage to mark the message boundary.  */
        if (IS_DTLS(ss)) {
            rv = dtls_StageHandshakeMessage(ss);
            if (rv != SECSuccess) {
                goto loser;
            }
        }

        /* As here the function takes the full message and hashes it in one go,
         * For DTLS1.3, we skip hashing the unnecessary header fields.
         * See ssl3_AppendHandshakeHeader. */
        if (IS_DTLS(ss) && ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3) {
            rv = ssl3_AppendHandshakeSuppressHash(ss, chBuf.buf, chBuf.len);
            if (rv != SECSuccess) {
                goto loser; /* code set */
            }
            if (!ss->firstHsDone) {
                PORT_Assert(type == client_hello_retransmit ||
                            ss->ssl3.hs.dtls13ClientMessageBuffer.len == 0);
                sslBuffer_Clear(&ss->ssl3.hs.dtls13ClientMessageBuffer);
                /* Here instead of computing the hash, we copy the data to a buffer.*/
                rv = sslBuffer_Append(&ss->ssl3.hs.dtls13ClientMessageBuffer, chBuf.buf, chBuf.len);
            }
        } else {
            rv = ssl3_AppendHandshake(ss, chBuf.buf, chBuf.len);
        }

    } else {
        PORT_Assert(!IS_DTLS(ss));
        rv = tls13_ConstructClientHelloWithEch(ss, sid, !requestingResume, &chBuf, &extensionBuf);
        if (rv != SECSuccess) {
            goto loser; /* code set */
        }
        rv = ssl3_UpdateDefaultHandshakeHashes(ss, chBuf.buf, chBuf.len);
        if (rv != SECSuccess) {
            goto loser; /* code set */
        }

        if (IS_DTLS(ss)) {
            rv = dtls_StageHandshakeMessage(ss);
            if (rv != SECSuccess) {
                goto loser;
            }
        }
        /* By default, all messagess are added to both the inner and
         * outer transcripts. For CH (or CH2 if HRR), that's problematic. */
        rv = ssl3_AppendHandshakeSuppressHash(ss, chBuf.buf, chBuf.len);
    }
    if (rv != SECSuccess) {
        goto loser;
    }

    if (unlockNeeded) {
        /* Note: goto loser can't be used past this point. */
        PR_RWLock_Unlock(sid->u.ssl3.lock);
    }

    if (ss->xtnData.sentSessionTicketInClientHello) {
        SSL_AtomicIncrementLong(&ssl3stats.sch_sid_stateless_resumes);
    }

    if (ss->ssl3.hs.sendingSCSV) {
        /* Since we sent the SCSV, pretend we sent empty RI extension. */
        TLSExtensionData *xtnData = &ss->xtnData;
        xtnData->advertised[xtnData->numAdvertised++] =
            ssl_renegotiation_info_xtn;
    }

    flags = 0;
    rv = ssl3_FlushHandshake(ss, flags);
    if (rv != SECSuccess) {
        return rv; /* error code set by ssl3_FlushHandshake */
    }

    if (version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        rv = tls13_MaybeDo0RTTHandshake(ss);
        if (rv != SECSuccess) {
            return SECFailure; /* error code set already. */
        }
    }

    ss->ssl3.hs.ws = wait_server_hello;
    sslBuffer_Clear(&chBuf);
    sslBuffer_Clear(&extensionBuf);
    return SECSuccess;

loser:
    if (unlockNeeded) {
        PR_RWLock_Unlock(sid->u.ssl3.lock);
    }
    sslBuffer_Clear(&chBuf);
    sslBuffer_Clear(&extensionBuf);
    return SECFailure;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered a
 * complete ssl3 Hello Request.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleHelloRequest(sslSocket *ss)
{
    sslSessionID *sid = ss->sec.ci.sid;
    SECStatus rv;

    SSL_TRC(3, ("%d: SSL3[%d]: handle hello_request handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->version < SSL_LIBRARY_VERSION_TLS_1_3);

    if (ss->ssl3.hs.ws == wait_server_hello)
        return SECSuccess;
    if (ss->ssl3.hs.ws != idle_handshake || ss->sec.isServer) {
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HELLO_REQUEST);
        return SECFailure;
    }
    if (ss->opt.enableRenegotiation == SSL_RENEGOTIATE_NEVER) {
        (void)SSL3_SendAlert(ss, alert_warning, no_renegotiation);
        PORT_SetError(SSL_ERROR_RENEGOTIATION_NOT_ALLOWED);
        return SECFailure;
    }

    if (sid) {
        ssl_UncacheSessionID(ss);
        ssl_FreeSID(sid);
        ss->sec.ci.sid = NULL;
    }

    if (IS_DTLS(ss)) {
        dtls_RehandshakeCleanup(ss);
    }

    ssl_GetXmitBufLock(ss);
    rv = ssl3_SendClientHello(ss, client_hello_renegotiation);
    ssl_ReleaseXmitBufLock(ss);

    return rv;
}

static const CK_MECHANISM_TYPE wrapMechanismList[SSL_NUM_WRAP_MECHS] = {
    CKM_DES3_ECB,
    CKM_CAST5_ECB,
    CKM_DES_ECB,
    CKM_KEY_WRAP_LYNKS,
    CKM_IDEA_ECB,
    CKM_CAST3_ECB,
    CKM_CAST_ECB,
    CKM_RC5_ECB,
    CKM_RC2_ECB,
    CKM_CDMF_ECB,
    CKM_SKIPJACK_WRAP,
    CKM_SKIPJACK_CBC64,
    CKM_AES_ECB,
    CKM_CAMELLIA_ECB,
    CKM_SEED_ECB
};

static SECStatus
ssl_FindIndexByWrapMechanism(CK_MECHANISM_TYPE mech, unsigned int *wrapMechIndex)
{
    unsigned int i;
    for (i = 0; i < SSL_NUM_WRAP_MECHS; ++i) {
        if (wrapMechanismList[i] == mech) {
            *wrapMechIndex = i;
            return SECSuccess;
        }
    }
    PORT_Assert(0);
    PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    return SECFailure;
}

/* Each process sharing the server session ID cache has its own array of SymKey
 * pointers for the symmetric wrapping keys that are used to wrap the master
 * secrets.  There is one key for each authentication type.  These Symkeys
 * correspond to the wrapped SymKeys kept in the server session cache.
 */
const SSLAuthType ssl_wrap_key_auth_type[SSL_NUM_WRAP_KEYS] = {
    ssl_auth_rsa_decrypt,
    ssl_auth_rsa_sign,
    ssl_auth_rsa_pss,
    ssl_auth_ecdsa,
    ssl_auth_ecdh_rsa,
    ssl_auth_ecdh_ecdsa
};

static SECStatus
ssl_FindIndexByWrapKey(const sslServerCert *serverCert, unsigned int *wrapKeyIndex)
{
    unsigned int i;
    for (i = 0; i < SSL_NUM_WRAP_KEYS; ++i) {
        if (SSL_CERT_IS(serverCert, ssl_wrap_key_auth_type[i])) {
            *wrapKeyIndex = i;
            return SECSuccess;
        }
    }
    /* Can't assert here because we still get people using DSA certificates. */
    PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    return SECFailure;
}

static PK11SymKey *
ssl_UnwrapSymWrappingKey(
    SSLWrappedSymWrappingKey *pWswk,
    SECKEYPrivateKey *svrPrivKey,
    unsigned int wrapKeyIndex,
    CK_MECHANISM_TYPE masterWrapMech,
    void *pwArg)
{
    PK11SymKey *unwrappedWrappingKey = NULL;
    SECItem wrappedKey;
    PK11SymKey *Ks;
    SECKEYPublicKey pubWrapKey;
    ECCWrappedKeyInfo *ecWrapped;

    /* found the wrapping key on disk. */
    PORT_Assert(pWswk->symWrapMechanism == masterWrapMech);
    PORT_Assert(pWswk->wrapKeyIndex == wrapKeyIndex);
    if (pWswk->symWrapMechanism != masterWrapMech ||
        pWswk->wrapKeyIndex != wrapKeyIndex) {
        goto loser;
    }
    wrappedKey.type = siBuffer;
    wrappedKey.data = pWswk->wrappedSymmetricWrappingkey;
    wrappedKey.len = pWswk->wrappedSymKeyLen;
    PORT_Assert(wrappedKey.len <= sizeof pWswk->wrappedSymmetricWrappingkey);

    switch (ssl_wrap_key_auth_type[wrapKeyIndex]) {

        case ssl_auth_rsa_decrypt:
        case ssl_auth_rsa_sign: /* bad: see Bug 1248320 */
            unwrappedWrappingKey =
                PK11_PubUnwrapSymKey(svrPrivKey, &wrappedKey,
                                     masterWrapMech, CKA_UNWRAP, 0);
            break;

        case ssl_auth_ecdsa:
        case ssl_auth_ecdh_rsa:
        case ssl_auth_ecdh_ecdsa:
            /*
             * For ssl_auth_ecd*, we first create an EC public key based on
             * data stored with the wrappedSymmetricWrappingkey. Next,
             * we do an ECDH computation involving this public key and
             * the SSL server's (long-term) EC private key. The resulting
             * shared secret is treated the same way as Fortezza's Ks, i.e.,
             * it is used to recover the symmetric wrapping key.
             *
             * The data in wrappedSymmetricWrappingkey is laid out as defined
             * in the ECCWrappedKeyInfo structure.
             */
            ecWrapped = (ECCWrappedKeyInfo *)pWswk->wrappedSymmetricWrappingkey;

            PORT_Assert(ecWrapped->encodedParamLen + ecWrapped->pubValueLen +
                            ecWrapped->wrappedKeyLen <=
                        MAX_EC_WRAPPED_KEY_BUFLEN);

            if (ecWrapped->encodedParamLen + ecWrapped->pubValueLen +
                    ecWrapped->wrappedKeyLen >
                MAX_EC_WRAPPED_KEY_BUFLEN) {
                PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
                goto loser;
            }

            pubWrapKey.keyType = ecKey;
            pubWrapKey.u.ec.size = ecWrapped->size;
            pubWrapKey.u.ec.DEREncodedParams.len = ecWrapped->encodedParamLen;
            pubWrapKey.u.ec.DEREncodedParams.data = ecWrapped->var;
            pubWrapKey.u.ec.publicValue.len = ecWrapped->pubValueLen;
            pubWrapKey.u.ec.publicValue.data = ecWrapped->var +
                                               ecWrapped->encodedParamLen;

            wrappedKey.len = ecWrapped->wrappedKeyLen;
            wrappedKey.data = ecWrapped->var + ecWrapped->encodedParamLen +
                              ecWrapped->pubValueLen;

            /* Derive Ks using ECDH */
            Ks = PK11_PubDeriveWithKDF(svrPrivKey, &pubWrapKey, PR_FALSE, NULL,
                                       NULL, CKM_ECDH1_DERIVE, masterWrapMech,
                                       CKA_DERIVE, 0, CKD_NULL, NULL, NULL);
            if (Ks == NULL) {
                goto loser;
            }

            /*  Use Ks to unwrap the wrapping key */
            unwrappedWrappingKey = PK11_UnwrapSymKey(Ks, masterWrapMech, NULL,
                                                     &wrappedKey, masterWrapMech,
                                                     CKA_UNWRAP, 0);
            PK11_FreeSymKey(Ks);

            break;

        default:
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            goto loser;
    }
loser:
    return unwrappedWrappingKey;
}

typedef struct {
    PK11SymKey *symWrapKey[SSL_NUM_WRAP_KEYS];
} ssl3SymWrapKey;

static PZLock *symWrapKeysLock = NULL;
static ssl3SymWrapKey symWrapKeys[SSL_NUM_WRAP_MECHS];

SECStatus
ssl_FreeSymWrapKeysLock(void)
{
    if (symWrapKeysLock) {
        PZ_DestroyLock(symWrapKeysLock);
        symWrapKeysLock = NULL;
        return SECSuccess;
    }
    PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
    return SECFailure;
}

SECStatus
SSL3_ShutdownServerCache(void)
{
    int i, j;

    if (!symWrapKeysLock)
        return SECSuccess; /* lock was never initialized */
    PZ_Lock(symWrapKeysLock);
    /* get rid of all symWrapKeys */
    for (i = 0; i < SSL_NUM_WRAP_MECHS; ++i) {
        for (j = 0; j < SSL_NUM_WRAP_KEYS; ++j) {
            PK11SymKey **pSymWrapKey;
            pSymWrapKey = &symWrapKeys[i].symWrapKey[j];
            if (*pSymWrapKey) {
                PK11_FreeSymKey(*pSymWrapKey);
                *pSymWrapKey = NULL;
            }
        }
    }

    PZ_Unlock(symWrapKeysLock);
    ssl_FreeSessionCacheLocks();
    return SECSuccess;
}

SECStatus
ssl_InitSymWrapKeysLock(void)
{
    symWrapKeysLock = PZ_NewLock(nssILockOther);
    return symWrapKeysLock ? SECSuccess : SECFailure;
}

/* Try to get wrapping key for mechanism from in-memory array.
 * If that fails, look for one on disk.
 * If that fails, generate a new one, put the new one on disk,
 * Put the new key in the in-memory array.
 *
 * Note that this function performs some fairly inadvisable functions with
 * certificate private keys.  ECDSA keys are used with ECDH; similarly, RSA
 * signing keys are used to encrypt.  Bug 1248320.
 */
PK11SymKey *
ssl3_GetWrappingKey(sslSocket *ss,
                    PK11SlotInfo *masterSecretSlot,
                    CK_MECHANISM_TYPE masterWrapMech,
                    void *pwArg)
{
    SSLAuthType authType;
    SECKEYPrivateKey *svrPrivKey;
    SECKEYPublicKey *svrPubKey = NULL;
    PK11SymKey *unwrappedWrappingKey = NULL;
    PK11SymKey **pSymWrapKey;
    CK_MECHANISM_TYPE asymWrapMechanism = CKM_INVALID_MECHANISM;
    int length;
    unsigned int wrapMechIndex;
    unsigned int wrapKeyIndex;
    SECStatus rv;
    SECItem wrappedKey;
    SSLWrappedSymWrappingKey wswk;
    PK11SymKey *Ks = NULL;
    SECKEYPublicKey *pubWrapKey = NULL;
    SECKEYPrivateKey *privWrapKey = NULL;
    ECCWrappedKeyInfo *ecWrapped;
    const sslServerCert *serverCert = ss->sec.serverCert;

    PORT_Assert(serverCert);
    PORT_Assert(serverCert->serverKeyPair);
    PORT_Assert(serverCert->serverKeyPair->privKey);
    PORT_Assert(serverCert->serverKeyPair->pubKey);
    if (!serverCert || !serverCert->serverKeyPair ||
        !serverCert->serverKeyPair->privKey ||
        !serverCert->serverKeyPair->pubKey) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return NULL; /* hmm */
    }

    rv = ssl_FindIndexByWrapKey(serverCert, &wrapKeyIndex);
    if (rv != SECSuccess)
        return NULL; /* unusable wrapping key. */

    rv = ssl_FindIndexByWrapMechanism(masterWrapMech, &wrapMechIndex);
    if (rv != SECSuccess)
        return NULL; /* invalid masterWrapMech. */

    authType = ssl_wrap_key_auth_type[wrapKeyIndex];
    svrPrivKey = serverCert->serverKeyPair->privKey;
    pSymWrapKey = &symWrapKeys[wrapMechIndex].symWrapKey[wrapKeyIndex];

    ssl_InitSessionCacheLocks(PR_TRUE);

    PZ_Lock(symWrapKeysLock);

    unwrappedWrappingKey = *pSymWrapKey;
    if (unwrappedWrappingKey != NULL) {
        if (PK11_VerifyKeyOK(unwrappedWrappingKey)) {
            unwrappedWrappingKey = PK11_ReferenceSymKey(unwrappedWrappingKey);
            goto done;
        }
        /* slot series has changed, so this key is no good any more. */
        PK11_FreeSymKey(unwrappedWrappingKey);
        *pSymWrapKey = unwrappedWrappingKey = NULL;
    }

    /* Try to get wrapped SymWrapping key out of the (disk) cache. */
    /* Following call fills in wswk on success. */
    rv = ssl_GetWrappingKey(wrapMechIndex, wrapKeyIndex, &wswk);
    if (rv == SECSuccess) {
        /* found the wrapped sym wrapping key on disk. */
        unwrappedWrappingKey =
            ssl_UnwrapSymWrappingKey(&wswk, svrPrivKey, wrapKeyIndex,
                                     masterWrapMech, pwArg);
        if (unwrappedWrappingKey) {
            goto install;
        }
    }

    if (!masterSecretSlot) /* caller doesn't want to create a new one. */
        goto loser;

    length = PK11_GetBestKeyLength(masterSecretSlot, masterWrapMech);
    /* Zero length means fixed key length algorithm, or error.
     * It's ambiguous.
     */
    unwrappedWrappingKey = PK11_KeyGen(masterSecretSlot, masterWrapMech, NULL,
                                       length, pwArg);
    if (!unwrappedWrappingKey) {
        goto loser;
    }

    /* Prepare the buffer to receive the wrappedWrappingKey,
     * the symmetric wrapping key wrapped using the server's pub key.
     */
    PORT_Memset(&wswk, 0, sizeof wswk); /* eliminate UMRs. */

    svrPubKey = serverCert->serverKeyPair->pubKey;
    wrappedKey.type = siBuffer;
    wrappedKey.len = SECKEY_PublicKeyStrength(svrPubKey);
    wrappedKey.data = wswk.wrappedSymmetricWrappingkey;

    PORT_Assert(wrappedKey.len <= sizeof wswk.wrappedSymmetricWrappingkey);
    if (wrappedKey.len > sizeof wswk.wrappedSymmetricWrappingkey)
        goto loser;

    /* wrap symmetric wrapping key in server's public key. */
    switch (authType) {
        case ssl_auth_rsa_decrypt:
        case ssl_auth_rsa_sign: /* bad: see Bug 1248320 */
        case ssl_auth_rsa_pss:
            asymWrapMechanism = CKM_RSA_PKCS;
            rv = PK11_PubWrapSymKey(asymWrapMechanism, svrPubKey,
                                    unwrappedWrappingKey, &wrappedKey);
            break;

        case ssl_auth_ecdsa:
        case ssl_auth_ecdh_rsa:
        case ssl_auth_ecdh_ecdsa:
            /*
             * We generate an ephemeral EC key pair. Perform an ECDH
             * computation involving this ephemeral EC public key and
             * the SSL server's (long-term) EC private key. The resulting
             * shared secret is treated in the same way as Fortezza's Ks,
             * i.e., it is used to wrap the wrapping key. To facilitate
             * unwrapping in ssl_UnwrapWrappingKey, we also store all
             * relevant info about the ephemeral EC public key in
             * wswk.wrappedSymmetricWrappingkey and lay it out as
             * described in the ECCWrappedKeyInfo structure.
             */
            PORT_Assert(SECKEY_GetPublicKeyType(svrPubKey) == ecKey);
            if (SECKEY_GetPublicKeyType(svrPubKey) != ecKey) {
                /* something is wrong in sslsecur.c if this isn't an ecKey */
                PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
                rv = SECFailure;
                goto ec_cleanup;
            }

            privWrapKey = SECKEY_CreateECPrivateKey(
                &svrPubKey->u.ec.DEREncodedParams, &pubWrapKey, NULL);
            if ((privWrapKey == NULL) || (pubWrapKey == NULL)) {
                rv = SECFailure;
                goto ec_cleanup;
            }

            /* Set the key size in bits */
            if (pubWrapKey->u.ec.size == 0) {
                pubWrapKey->u.ec.size = SECKEY_PublicKeyStrengthInBits(svrPubKey);
            }

            PORT_Assert(pubWrapKey->u.ec.DEREncodedParams.len +
                            pubWrapKey->u.ec.publicValue.len <
                        MAX_EC_WRAPPED_KEY_BUFLEN);
            if (pubWrapKey->u.ec.DEREncodedParams.len +
                    pubWrapKey->u.ec.publicValue.len >=
                MAX_EC_WRAPPED_KEY_BUFLEN) {
                PORT_SetError(SEC_ERROR_INVALID_KEY);
                rv = SECFailure;
                goto ec_cleanup;
            }

            /* Derive Ks using ECDH */
            Ks = PK11_PubDeriveWithKDF(svrPrivKey, pubWrapKey, PR_FALSE, NULL,
                                       NULL, CKM_ECDH1_DERIVE, masterWrapMech,
                                       CKA_DERIVE, 0, CKD_NULL, NULL, NULL);
            if (Ks == NULL) {
                rv = SECFailure;
                goto ec_cleanup;
            }

            ecWrapped = (ECCWrappedKeyInfo *)(wswk.wrappedSymmetricWrappingkey);
            ecWrapped->size = pubWrapKey->u.ec.size;
            ecWrapped->encodedParamLen = pubWrapKey->u.ec.DEREncodedParams.len;
            PORT_Memcpy(ecWrapped->var, pubWrapKey->u.ec.DEREncodedParams.data,
                        pubWrapKey->u.ec.DEREncodedParams.len);

            ecWrapped->pubValueLen = pubWrapKey->u.ec.publicValue.len;
            PORT_Memcpy(ecWrapped->var + ecWrapped->encodedParamLen,
                        pubWrapKey->u.ec.publicValue.data,
                        pubWrapKey->u.ec.publicValue.len);

            wrappedKey.len = MAX_EC_WRAPPED_KEY_BUFLEN -
                             (ecWrapped->encodedParamLen + ecWrapped->pubValueLen);
            wrappedKey.data = ecWrapped->var + ecWrapped->encodedParamLen +
                              ecWrapped->pubValueLen;

            /* wrap symmetricWrapping key with the local Ks */
            rv = PK11_WrapSymKey(masterWrapMech, NULL, Ks,
                                 unwrappedWrappingKey, &wrappedKey);

            if (rv != SECSuccess) {
                goto ec_cleanup;
            }

            /* Write down the length of wrapped key in the buffer
             * wswk.wrappedSymmetricWrappingkey at the appropriate offset
             */
            ecWrapped->wrappedKeyLen = wrappedKey.len;

        ec_cleanup:
            if (privWrapKey)
                SECKEY_DestroyPrivateKey(privWrapKey);
            if (pubWrapKey)
                SECKEY_DestroyPublicKey(pubWrapKey);
            if (Ks)
                PK11_FreeSymKey(Ks);
            asymWrapMechanism = masterWrapMech;
            break;

        default:
            rv = SECFailure;
            break;
    }

    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    PORT_Assert(asymWrapMechanism != CKM_INVALID_MECHANISM);

    wswk.symWrapMechanism = masterWrapMech;
    wswk.asymWrapMechanism = asymWrapMechanism;
    wswk.wrapMechIndex = wrapMechIndex;
    wswk.wrapKeyIndex = wrapKeyIndex;
    wswk.wrappedSymKeyLen = wrappedKey.len;

    /* put it on disk. */
    /* If the wrapping key for this KEA type has already been set,
     * then abandon the value we just computed and
     * use the one we got from the disk.
     */
    rv = ssl_SetWrappingKey(&wswk);
    if (rv == SECSuccess) {
        /* somebody beat us to it.  The original contents of our wswk
         * has been replaced with the content on disk.  Now, discard
         * the key we just created and unwrap this new one.
         */
        PK11_FreeSymKey(unwrappedWrappingKey);

        unwrappedWrappingKey =
            ssl_UnwrapSymWrappingKey(&wswk, svrPrivKey, wrapKeyIndex,
                                     masterWrapMech, pwArg);
    }

install:
    if (unwrappedWrappingKey) {
        *pSymWrapKey = PK11_ReferenceSymKey(unwrappedWrappingKey);
    }

loser:
done:
    PZ_Unlock(symWrapKeysLock);
    return unwrappedWrappingKey;
}

#ifdef NSS_ALLOW_SSLKEYLOGFILE
/* hexEncode hex encodes |length| bytes from |in| and writes it as |length*2|
 * bytes to |out|. */
static void
hexEncode(char *out, const unsigned char *in, unsigned int length)
{
    static const char hextable[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < length; i++) {
        *(out++) = hextable[in[i] >> 4];
        *(out++) = hextable[in[i] & 15];
    }
}
#endif

/* Called from ssl3_SendClientKeyExchange(). */
static SECStatus
ssl3_SendRSAClientKeyExchange(sslSocket *ss, SECKEYPublicKey *svrPubKey)
{
    PK11SymKey *pms = NULL;
    SECStatus rv = SECFailure;
    SECItem enc_pms = { siBuffer, NULL, 0 };
    PRBool isTLS;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    /* Generate the pre-master secret ...  */
    ssl_GetSpecWriteLock(ss);
    isTLS = (PRBool)(ss->version > SSL_LIBRARY_VERSION_3_0);

    pms = ssl3_GenerateRSAPMS(ss, ss->ssl3.pwSpec, NULL);
    ssl_ReleaseSpecWriteLock(ss);
    if (pms == NULL) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    /* Get the wrapped (encrypted) pre-master secret, enc_pms */
    unsigned int svrPubKeyBits = SECKEY_PublicKeyStrengthInBits(svrPubKey);
    enc_pms.len = (svrPubKeyBits + 7) / 8;
    /* Check that the RSA key isn't larger than 8k bit. */
    if (svrPubKeyBits > SSL_MAX_RSA_KEY_BITS) {
        (void)SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }
    enc_pms.data = (unsigned char *)PORT_Alloc(enc_pms.len);
    if (enc_pms.data == NULL) {
        goto loser; /* err set by PORT_Alloc */
    }

    /* Wrap pre-master secret in server's public key. */
    rv = PK11_PubWrapSymKey(CKM_RSA_PKCS, svrPubKey, pms, &enc_pms);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

#ifdef TRACE
    if (ssl_trace >= 100) {
        SECStatus extractRV = PK11_ExtractKeyValue(pms);
        if (extractRV == SECSuccess) {
            SECItem *keyData = PK11_GetKeyData(pms);
            if (keyData && keyData->data && keyData->len) {
                ssl_PrintBuf(ss, "Pre-Master Secret",
                             keyData->data, keyData->len);
            }
        }
    }
#endif

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_client_key_exchange,
                                    isTLS ? enc_pms.len + 2
                                          : enc_pms.len);
    if (rv != SECSuccess) {
        goto loser; /* err set by ssl3_AppendHandshake* */
    }
    if (isTLS) {
        rv = ssl3_AppendHandshakeVariable(ss, enc_pms.data, enc_pms.len, 2);
    } else {
        rv = ssl3_AppendHandshake(ss, enc_pms.data, enc_pms.len);
    }
    if (rv != SECSuccess) {
        goto loser; /* err set by ssl3_AppendHandshake* */
    }

    rv = ssl3_InitPendingCipherSpecs(ss, pms, PR_TRUE);
    PK11_FreeSymKey(pms);
    pms = NULL;

    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    rv = SECSuccess;

loser:
    if (enc_pms.data != NULL) {
        PORT_Free(enc_pms.data);
    }
    if (pms != NULL) {
        PK11_FreeSymKey(pms);
    }
    return rv;
}

/* DH shares need to be padded to the size of their prime.  Some implementations
 * require this.  TLS 1.3 also requires this. */
SECStatus
ssl_AppendPaddedDHKeyShare(sslBuffer *buf, const SECKEYPublicKey *pubKey,
                           PRBool appendLength)
{
    SECStatus rv;
    unsigned int pad = pubKey->u.dh.prime.len - pubKey->u.dh.publicValue.len;

    if (appendLength) {
        rv = sslBuffer_AppendNumber(buf, pubKey->u.dh.prime.len, 2);
        if (rv != SECSuccess) {
            return rv;
        }
    }
    while (pad) {
        rv = sslBuffer_AppendNumber(buf, 0, 1);
        if (rv != SECSuccess) {
            return rv;
        }
        --pad;
    }
    rv = sslBuffer_Append(buf, pubKey->u.dh.publicValue.data,
                          pubKey->u.dh.publicValue.len);
    if (rv != SECSuccess) {
        return rv;
    }
    return SECSuccess;
}

/* Called from ssl3_SendClientKeyExchange(). */
static SECStatus
ssl3_SendDHClientKeyExchange(sslSocket *ss, SECKEYPublicKey *svrPubKey)
{
    PK11SymKey *pms = NULL;
    SECStatus rv;
    PRBool isTLS;
    CK_MECHANISM_TYPE target;

    const ssl3DHParams *params;
    ssl3DHParams customParams;
    const sslNamedGroupDef *groupDef;
    static const sslNamedGroupDef customGroupDef = {
        ssl_grp_ffdhe_custom, 0, ssl_kea_dh, SEC_OID_TLS_DHE_CUSTOM, PR_FALSE
    };
    sslEphemeralKeyPair *keyPair = NULL;
    SECKEYPublicKey *pubKey;
    PRUint8 dhData[SSL_MAX_DH_KEY_BITS / 8 + 2];
    sslBuffer dhBuf = SSL_BUFFER(dhData);

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    isTLS = (PRBool)(ss->version > SSL_LIBRARY_VERSION_3_0);

    /* Copy DH parameters from server key */

    if (SECKEY_GetPublicKeyType(svrPubKey) != dhKey) {
        PORT_SetError(SEC_ERROR_BAD_KEY);
        return SECFailure;
    }

    /* Work out the parameters. */
    rv = ssl_ValidateDHENamedGroup(ss, &svrPubKey->u.dh.prime,
                                   &svrPubKey->u.dh.base,
                                   &groupDef, &params);
    if (rv != SECSuccess) {
        /* If we require named groups, we will have already validated the group
         * in ssl_HandleDHServerKeyExchange() */
        PORT_Assert(!ss->opt.requireDHENamedGroups &&
                    !ss->xtnData.peerSupportsFfdheGroups);

        customParams.name = ssl_grp_ffdhe_custom;
        customParams.prime.data = svrPubKey->u.dh.prime.data;
        customParams.prime.len = svrPubKey->u.dh.prime.len;
        customParams.base.data = svrPubKey->u.dh.base.data;
        customParams.base.len = svrPubKey->u.dh.base.len;
        params = &customParams;
        groupDef = &customGroupDef;
    }
    ss->sec.keaGroup = groupDef;

    rv = ssl_CreateDHEKeyPair(groupDef, params, &keyPair);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SEC_ERROR_KEYGEN_FAIL);
        goto loser;
    }
    pubKey = keyPair->keys->pubKey;
    PRINT_BUF(50, (ss, "DH public value:",
                   pubKey->u.dh.publicValue.data,
                   pubKey->u.dh.publicValue.len));

    if (isTLS)
        target = CKM_TLS_MASTER_KEY_DERIVE_DH;
    else
        target = CKM_SSL3_MASTER_KEY_DERIVE_DH;

    /* Determine the PMS */
    pms = PK11_PubDerive(keyPair->keys->privKey, svrPubKey,
                         PR_FALSE, NULL, NULL, CKM_DH_PKCS_DERIVE,
                         target, CKA_DERIVE, 0, NULL);

    if (pms == NULL) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    /* Note: send the DH share padded to avoid triggering bugs. */
    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_client_key_exchange,
                                    params->prime.len + 2);
    if (rv != SECSuccess) {
        goto loser; /* err set by ssl3_AppendHandshake* */
    }
    rv = ssl_AppendPaddedDHKeyShare(&dhBuf, pubKey, PR_TRUE);
    if (rv != SECSuccess) {
        goto loser; /* err set by ssl_AppendPaddedDHKeyShare */
    }
    rv = ssl3_AppendBufferToHandshake(ss, &dhBuf);
    if (rv != SECSuccess) {
        goto loser; /* err set by ssl3_AppendBufferToHandshake */
    }

    rv = ssl3_InitPendingCipherSpecs(ss, pms, PR_TRUE);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    sslBuffer_Clear(&dhBuf);
    PK11_FreeSymKey(pms);
    ssl_FreeEphemeralKeyPair(keyPair);
    return SECSuccess;

loser:
    if (pms)
        PK11_FreeSymKey(pms);
    if (keyPair)
        ssl_FreeEphemeralKeyPair(keyPair);
    sslBuffer_Clear(&dhBuf);
    return SECFailure;
}

/* Called from ssl3_HandleServerHelloDone(). */
static SECStatus
ssl3_SendClientKeyExchange(sslSocket *ss)
{
    SECKEYPublicKey *serverKey = NULL;
    SECStatus rv = SECFailure;

    SSL_TRC(3, ("%d: SSL3[%d]: send client_key_exchange handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->sec.peerKey == NULL) {
        serverKey = CERT_ExtractPublicKey(ss->sec.peerCert);
        if (serverKey == NULL) {
            ssl_MapLowLevelError(SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE);
            return SECFailure;
        }
    } else {
        serverKey = ss->sec.peerKey;
        ss->sec.peerKey = NULL; /* we're done with it now */
    }

    ss->sec.keaType = ss->ssl3.hs.kea_def->exchKeyType;
    ss->sec.keaKeyBits = SECKEY_PublicKeyStrengthInBits(serverKey);

    switch (ss->ssl3.hs.kea_def->exchKeyType) {
        case ssl_kea_rsa:
            rv = ssl3_SendRSAClientKeyExchange(ss, serverKey);
            break;

        case ssl_kea_dh:
            rv = ssl3_SendDHClientKeyExchange(ss, serverKey);
            break;

        case ssl_kea_ecdh:
            rv = ssl3_SendECDHClientKeyExchange(ss, serverKey);
            break;

        default:
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            break;
    }

    SSL_TRC(3, ("%d: SSL3[%d]: DONE sending client_key_exchange",
                SSL_GETPID(), ss->fd));

    SECKEY_DestroyPublicKey(serverKey);
    return rv; /* err code already set. */
}

/* Used by ssl_PickSignatureScheme(). */
PRBool
ssl_CanUseSignatureScheme(SSLSignatureScheme scheme,
                          const SSLSignatureScheme *peerSchemes,
                          unsigned int peerSchemeCount,
                          PRBool requireSha1,
                          PRBool slotDoesPss)
{
    SSLHashType hashType;
    unsigned int i;

    /* Skip RSA-PSS schemes when the certificate's private key slot does
     * not support this signature mechanism. */
    if (ssl_IsRsaPssSignatureScheme(scheme) && !slotDoesPss) {
        return PR_FALSE;
    }

    hashType = ssl_SignatureSchemeToHashType(scheme);
    if (requireSha1 && (hashType != ssl_hash_sha1)) {
        return PR_FALSE;
    }

    if (!ssl_SchemePolicyOK(scheme, kSSLSigSchemePolicy)) {
        return PR_FALSE;
    }

    for (i = 0; i < peerSchemeCount; i++) {
        if (peerSchemes[i] == scheme) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

SECStatus
ssl_PrivateKeySupportsRsaPss(SECKEYPrivateKey *privKey, CERTCertificate *cert,
                             void *pwarg, PRBool *supportsRsaPss)
{
    PK11SlotInfo *slot = NULL;
    if (privKey) {
        slot = PK11_GetSlotFromPrivateKey(privKey);
    } else {
        CK_OBJECT_HANDLE certID = PK11_FindObjectForCert(cert, pwarg, &slot);
        if (certID == CK_INVALID_HANDLE) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
    }
    if (!slot) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    *supportsRsaPss = PK11_DoesMechanism(slot, auth_alg_defs[ssl_auth_rsa_pss]);
    PK11_FreeSlot(slot);
    return SECSuccess;
}

SECStatus
ssl_PickSignatureScheme(sslSocket *ss,
                        CERTCertificate *cert,
                        SECKEYPublicKey *pubKey,
                        SECKEYPrivateKey *privKey,
                        const SSLSignatureScheme *peerSchemes,
                        unsigned int peerSchemeCount,
                        PRBool requireSha1,
                        SSLSignatureScheme *schemePtr)
{
    unsigned int i;
    PRBool doesRsaPss;
    PRBool isTLS13 = ss->version >= SSL_LIBRARY_VERSION_TLS_1_3;
    SECStatus rv;
    SSLSignatureScheme scheme;
    SECOidTag spkiOid;

    /* We can't require SHA-1 in TLS 1.3. */
    PORT_Assert(!(requireSha1 && isTLS13));
    if (!pubKey || !cert) {
        PORT_Assert(0);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    rv = ssl_PrivateKeySupportsRsaPss(privKey, cert, ss->pkcs11PinArg,
                                      &doesRsaPss);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    /* If the certificate SPKI indicates a single scheme, don't search. */
    rv = ssl_SignatureSchemeFromSpki(&cert->subjectPublicKeyInfo,
                                     isTLS13, &scheme);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (scheme != ssl_sig_none) {
        if (!ssl_SignatureSchemeEnabled(ss, scheme) ||
            !ssl_CanUseSignatureScheme(scheme, peerSchemes, peerSchemeCount,
                                       requireSha1, doesRsaPss)) {
            PORT_SetError(SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM);
            return SECFailure;
        }
        *schemePtr = scheme;
        return SECSuccess;
    }

    spkiOid = SECOID_GetAlgorithmTag(&cert->subjectPublicKeyInfo.algorithm);
    if (spkiOid == SEC_OID_UNKNOWN) {
        return SECFailure;
    }

    /* Now we have to search based on the key type. Go through our preferred
     * schemes in order and find the first that can be used. */
    for (i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
        scheme = ss->ssl3.signatureSchemes[i];

        if (ssl_SignatureSchemeValid(scheme, spkiOid, isTLS13) &&
            ssl_CanUseSignatureScheme(scheme, peerSchemes, peerSchemeCount,
                                      requireSha1, doesRsaPss)) {
            *schemePtr = scheme;
            return SECSuccess;
        }
    }

    PORT_SetError(SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM);
    return SECFailure;
}

static SECStatus
ssl_PickFallbackSignatureScheme(sslSocket *ss, SECKEYPublicKey *pubKey)
{
    PRBool isTLS12 = ss->version >= SSL_LIBRARY_VERSION_TLS_1_2;

    switch (SECKEY_GetPublicKeyType(pubKey)) {
        case rsaKey:
            if (isTLS12) {
                ss->ssl3.hs.signatureScheme = ssl_sig_rsa_pkcs1_sha1;
            } else {
                ss->ssl3.hs.signatureScheme = ssl_sig_rsa_pkcs1_sha1md5;
            }
            break;
        case ecKey:
            ss->ssl3.hs.signatureScheme = ssl_sig_ecdsa_sha1;
            break;
        case dsaKey:
            ss->ssl3.hs.signatureScheme = ssl_sig_dsa_sha1;
            break;
        default:
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_INVALID_KEY);
            return SECFailure;
    }
    return SECSuccess;
}

/* ssl3_PickServerSignatureScheme selects a signature scheme for signing the
 * handshake.  Most of this is determined by the key pair we are using.
 * Prior to TLS 1.2, the MD5/SHA1 combination is always used. With TLS 1.2, a
 * client may advertise its support for signature and hash combinations. */
static SECStatus
ssl3_PickServerSignatureScheme(sslSocket *ss)
{
    const sslServerCert *cert = ss->sec.serverCert;
    PRBool isTLS12 = ss->version >= SSL_LIBRARY_VERSION_TLS_1_2;

    if (!isTLS12 || !ssl3_ExtensionNegotiated(ss, ssl_signature_algorithms_xtn)) {
        /* If the client didn't provide any signature_algorithms extension then
         * we can assume that they support SHA-1: RFC5246, Section 7.4.1.4.1. */
        return ssl_PickFallbackSignatureScheme(ss, cert->serverKeyPair->pubKey);
    }

    /* Sets error code, if needed. */
    return ssl_PickSignatureScheme(ss, cert->serverCert,
                                   cert->serverKeyPair->pubKey,
                                   cert->serverKeyPair->privKey,
                                   ss->xtnData.sigSchemes,
                                   ss->xtnData.numSigSchemes,
                                   PR_FALSE /* requireSha1 */,
                                   &ss->ssl3.hs.signatureScheme);
}

SECStatus
ssl_PickClientSignatureScheme(sslSocket *ss, CERTCertificate *clientCertificate,
                              SECKEYPrivateKey *privKey,
                              const SSLSignatureScheme *schemes,
                              unsigned int numSchemes,
                              SSLSignatureScheme *schemePtr)
{
    SECStatus rv;
    PRBool isTLS13 = (PRBool)ss->version >= SSL_LIBRARY_VERSION_TLS_1_3;
    SECKEYPublicKey *pubKey = CERT_ExtractPublicKey(clientCertificate);

    PORT_Assert(pubKey);

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_2) {
        /* We should have already checked that a signature scheme was
         * listed in the request. */
        PORT_Assert(schemes && numSchemes > 0);
    }

    if (!isTLS13 &&
        (SECKEY_GetPublicKeyType(pubKey) == rsaKey ||
         SECKEY_GetPublicKeyType(pubKey) == dsaKey) &&
        SECKEY_PublicKeyStrengthInBits(pubKey) <= 1024) {
        /* If the key is a 1024-bit RSA or DSA key, assume conservatively that
         * it may be unable to sign SHA-256 hashes. This is the case for older
         * Estonian ID cards that have 1024-bit RSA keys. In FIPS 186-2 and
         * older, DSA key size is at most 1024 bits and the hash function must
         * be SHA-1.
         */
        rv = ssl_PickSignatureScheme(ss, clientCertificate,
                                     pubKey, privKey, schemes, numSchemes,
                                     PR_TRUE /* requireSha1 */, schemePtr);
        if (rv == SECSuccess) {
            SECKEY_DestroyPublicKey(pubKey);
            return SECSuccess;
        }
        /* If this fails, that's because the peer doesn't advertise SHA-1,
         * so fall back to the full negotiation. */
    }
    rv = ssl_PickSignatureScheme(ss, clientCertificate,
                                 pubKey, privKey, schemes, numSchemes,
                                 PR_FALSE /* requireSha1 */, schemePtr);
    SECKEY_DestroyPublicKey(pubKey);
    return rv;
}

/* Called from ssl3_HandleServerHelloDone(). */
static SECStatus
ssl3_SendCertificateVerify(sslSocket *ss, SECKEYPrivateKey *privKey)
{
    SECStatus rv = SECFailure;
    PRBool isTLS12;
    SECItem buf = { siBuffer, NULL, 0 };
    SSL3Hashes hashes;
    unsigned int len;
    SSLHashType hashAlg;

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: SSL3[%d]: send certificate_verify handshake",
                SSL_GETPID(), ss->fd));

    ssl_GetSpecReadLock(ss);

    if (ss->ssl3.hs.hashType == handshake_hash_record) {
        hashAlg = ssl_SignatureSchemeToHashType(ss->ssl3.hs.signatureScheme);
    } else {
        /* Use ssl_hash_none to represent the MD5+SHA1 combo. */
        hashAlg = ssl_hash_none;
    }
    if (ss->ssl3.hs.hashType == handshake_hash_record &&
        hashAlg != ssl3_GetSuitePrfHash(ss)) {
        rv = ssl3_ComputeHandshakeHash(ss->ssl3.hs.messages.buf,
                                       ss->ssl3.hs.messages.len,
                                       hashAlg, &hashes);
        if (rv != SECSuccess) {
            ssl_MapLowLevelError(SSL_ERROR_DIGEST_FAILURE);
        }
    } else {
        rv = ssl3_ComputeHandshakeHashes(ss, ss->ssl3.pwSpec, &hashes, 0);
    }
    ssl_ReleaseSpecReadLock(ss);
    if (rv != SECSuccess) {
        goto done; /* err code was set by ssl3_ComputeHandshakeHash(es) */
    }

    isTLS12 = (PRBool)(ss->version == SSL_LIBRARY_VERSION_TLS_1_2);
    PORT_Assert(ss->version <= SSL_LIBRARY_VERSION_TLS_1_2);

    rv = ssl3_SignHashes(ss, &hashes, privKey, &buf);
    if (rv == SECSuccess && !ss->sec.isServer) {
        /* Remember the info about the slot that did the signing.
        ** Later, when doing an SSL restart handshake, verify this.
        ** These calls are mere accessors, and can't fail.
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

    len = buf.len + 2 + (isTLS12 ? 2 : 0);

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate_verify, len);
    if (rv != SECSuccess) {
        goto done; /* error code set by AppendHandshake */
    }
    if (isTLS12) {
        rv = ssl3_AppendHandshakeNumber(ss, ss->ssl3.hs.signatureScheme, 2);
        if (rv != SECSuccess) {
            goto done; /* err set by AppendHandshake. */
        }
    }
    rv = ssl3_AppendHandshakeVariable(ss, buf.data, buf.len, 2);
    if (rv != SECSuccess) {
        goto done; /* error code set by AppendHandshake */
    }

done:
    if (buf.data)
        PORT_Free(buf.data);
    return rv;
}

/* Once a cipher suite has been selected, make sure that the necessary secondary
 * information is properly set. */
SECStatus
ssl3_SetupCipherSuite(sslSocket *ss, PRBool initHashes)
{
    ss->ssl3.hs.suite_def = ssl_LookupCipherSuiteDef(ss->ssl3.hs.cipher_suite);
    if (!ss->ssl3.hs.suite_def) {
        PORT_Assert(0);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    ss->ssl3.hs.kea_def = &kea_defs[ss->ssl3.hs.suite_def->key_exchange_alg];
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_cipher_suite;

    if (!initHashes) {
        return SECSuccess;
    }
    /* Now we have a cipher suite, initialize the handshake hashes. */
    return ssl3_InitHandshakeHashes(ss);
}

SECStatus
ssl_ClientSetCipherSuite(sslSocket *ss, SSL3ProtocolVersion version,
                         ssl3CipherSuite suite, PRBool initHashes)
{
    unsigned int i;
    if (ssl3_config_match_init(ss) == 0) {
        PORT_Assert(PR_FALSE);
        return SECFailure;
    }
    for (i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        ssl3CipherSuiteCfg *suiteCfg = &ss->cipherSuites[i];
        if (suite == suiteCfg->cipher_suite) {
            SSLVersionRange vrange = { version, version };
            if (!ssl3_config_match(suiteCfg, ss->ssl3.policy, &vrange, ss)) {
                /* config_match already checks whether the cipher suite is
                 * acceptable for the version, but the check is repeated here
                 * in order to give a more precise error code. */
                if (!ssl3_CipherSuiteAllowedForVersionRange(suite, &vrange)) {
                    PORT_SetError(SSL_ERROR_CIPHER_DISALLOWED_FOR_VERSION);
                } else {
                    PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
                }
                return SECFailure;
            }
            break;
        }
    }
    if (i >= ssl_V3_SUITES_IMPLEMENTED) {
        PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
        return SECFailure;
    }

    /* Don't let the server change its mind. */
    if (ss->ssl3.hs.helloRetry && suite != ss->ssl3.hs.cipher_suite) {
        (void)SSL3_SendAlert(ss, alert_fatal, illegal_parameter);
        PORT_SetError(SSL_ERROR_RX_MALFORMED_SERVER_HELLO);
        return SECFailure;
    }

    ss->ssl3.hs.cipher_suite = (ssl3CipherSuite)suite;
    return ssl3_SetupCipherSuite(ss, initHashes);
}

/* Check that session ID we received from the server, if any, matches our
 * expectations, depending on whether we're in compat mode and whether we
 * negotiated TLS 1.3+ or TLS 1.2-.
 */
static PRBool
ssl_CheckServerSessionIdCorrectness(sslSocket *ss, SECItem *sidBytes)
{
    sslSessionID *sid = ss->sec.ci.sid;
    PRBool sidMatch = PR_FALSE;
    PRBool sentFakeSid = PR_FALSE;
    PRBool sentRealSid = sid && sid->version < SSL_LIBRARY_VERSION_TLS_1_3;

    /* If attempting to resume a TLS 1.2 connection, the session ID won't be a
     * fake. Check for the real value. */
    if (sentRealSid) {
        sidMatch = (sidBytes->len == sid->u.ssl3.sessionIDLength) &&
                   (!sidBytes->len || PORT_Memcmp(sid->u.ssl3.sessionID, sidBytes->data, sidBytes->len) == 0);
    } else {
        /* Otherwise, the session ID was a fake if TLS 1.3 compat mode is
         * enabled.  If so, check for the fake value. */
        sentFakeSid = ss->opt.enableTls13CompatMode && !IS_DTLS(ss);
        if (sentFakeSid && sidBytes->len == SSL3_SESSIONID_BYTES) {
            PRUint8 buf[SSL3_SESSIONID_BYTES];
            ssl_MakeFakeSid(ss, buf);
            sidMatch = PORT_Memcmp(buf, sidBytes->data, sidBytes->len) == 0;
        }
    }

    /* TLS 1.2: Session ID shouldn't match if we sent a fake. */
    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        if (sentFakeSid) {
            return !sidMatch;
        }
        return PR_TRUE;
    }

    /* TLS 1.3: We sent a session ID.  The server's should match. */
    if (!IS_DTLS(ss) && (sentRealSid || sentFakeSid)) {
        return sidMatch;
    }

    /* TLS 1.3 (no SID)/DTLS 1.3: The server shouldn't send a session ID. */
    return sidBytes->len == 0;
}

static SECStatus
ssl_CheckServerRandom(sslSocket *ss)
{
    /* Check the ServerHello.random per [RFC 8446 Section 4.1.3].
     *
     * TLS 1.3 clients receiving a ServerHello indicating TLS 1.2 or below
     * MUST check that the last 8 bytes are not equal to either of these
     * values.  TLS 1.2 clients SHOULD also check that the last 8 bytes are
     * not equal to the second value if the ServerHello indicates TLS 1.1 or
     * below.  If a match is found, the client MUST abort the handshake with
     * an "illegal_parameter" alert.
     */
    SSL3ProtocolVersion checkVersion =
        ss->ssl3.downgradeCheckVersion ? ss->ssl3.downgradeCheckVersion
                                       : ss->vrange.max;

    if (checkVersion >= SSL_LIBRARY_VERSION_TLS_1_2 &&
        checkVersion > ss->version) {
        /* Both sections use the same sentinel region. */
        PRUint8 *downgrade_sentinel =
            ss->ssl3.hs.server_random +
            SSL3_RANDOM_LENGTH - sizeof(tls12_downgrade_random);

        if (!PORT_Memcmp(downgrade_sentinel,
                         tls12_downgrade_random,
                         sizeof(tls12_downgrade_random)) ||
            !PORT_Memcmp(downgrade_sentinel,
                         tls1_downgrade_random,
                         sizeof(tls1_downgrade_random))) {
            return SECFailure;
        }
    }

    return SECSuccess;
}

/* Called from ssl3_HandleHandshakeMessage() when it has deciphered a complete
 * ssl3 ServerHello message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleServerHello(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    PRUint32 cipher;
    int errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
    PRUint32 compression;
    SECStatus rv;
    SECItem sidBytes = { siBuffer, NULL, 0 };
    PRBool isHelloRetry;
    SSL3AlertDescription desc = illegal_parameter;
    const PRUint8 *savedMsg = b;
    const PRUint32 savedLength = length;

    SSL_TRC(3, ("%d: SSL3[%d]: handle server_hello handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->ssl3.hs.ws != wait_server_hello) {
        errCode = SSL_ERROR_RX_UNEXPECTED_SERVER_HELLO;
        desc = unexpected_message;
        goto alert_loser;
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
    // TODO(djackson) - Bob removed this. Why?
    if (ss->ssl3.hs.clientAuthSignatureSchemes != NULL) {
        PR_Free(ss->ssl3.hs.clientAuthSignatureSchemes);
        ss->ssl3.hs.clientAuthSignatureSchemes = NULL;
        ss->ssl3.hs.clientAuthSignatureSchemesLen = 0;
    }

    /* Note that if the server selects TLS 1.3, this will set the version to TLS
     * 1.2.  We will amend that once all other fields have been read. */
    rv = ssl_ClientReadVersion(ss, &b, &length, &ss->version);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }

    rv = ssl3_ConsumeHandshake(
        ss, ss->ssl3.hs.server_random, SSL3_RANDOM_LENGTH, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }
    isHelloRetry = !PORT_Memcmp(ss->ssl3.hs.server_random,
                                ssl_hello_retry_random, SSL3_RANDOM_LENGTH);

    rv = ssl3_ConsumeHandshakeVariable(ss, &sidBytes, 1, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }
    if (sidBytes.len > SSL3_SESSIONID_BYTES) {
        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_0)
            desc = decode_error;
        goto alert_loser; /* malformed. */
    }

    /* Read the cipher suite. */
    rv = ssl3_ConsumeHandshakeNumber(ss, &cipher, 2, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }

    /* Compression method. */
    rv = ssl3_ConsumeHandshakeNumber(ss, &compression, 1, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* alert has been sent */
    }
    if (compression != ssl_compression_null) {
        desc = illegal_parameter;
        errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
        goto alert_loser;
    }

    /* Parse extensions. */
    if (length != 0) {
        PRUint32 extensionLength;
        rv = ssl3_ConsumeHandshakeNumber(ss, &extensionLength, 2, &b, &length);
        if (rv != SECSuccess) {
            goto loser; /* alert already sent */
        }
        if (extensionLength != length) {
            desc = decode_error;
            goto alert_loser;
        }
        rv = ssl3_ParseExtensions(ss, &b, &length);
        if (rv != SECSuccess) {
            goto alert_loser; /* malformed */
        }
    }

    /* Read supported_versions if present. */
    rv = tls13_ClientReadSupportedVersion(ss);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* RFC 9147. 5.2.
     * DTLS Handshake Message Format states the difference between the computation
     * of the transcript if the version is DTLS1.2 or DTLS1.3.
     *
     * At this moment we are sure which version
     * we are planning to use during the connection, so we can compute the hash. */
    rv = ssl3_MaybeUpdateHashWithSavedRecord(ss);
    if (rv != SECSuccess) {
        goto loser;
    }

    PORT_Assert(!SSL_ALL_VERSIONS_DISABLED(&ss->vrange));
    /* Check that the version is within the configured range. */
    if (ss->vrange.min > ss->version || ss->vrange.max < ss->version) {
        desc = (ss->version > SSL_LIBRARY_VERSION_3_0)
                   ? protocol_version
                   : handshake_failure;
        errCode = SSL_ERROR_UNSUPPORTED_VERSION;
        goto alert_loser;
    }

    if (isHelloRetry && ss->ssl3.hs.helloRetry) {
        SSL_TRC(3, ("%d: SSL3[%d]: received a second hello_retry_request",
                    SSL_GETPID(), ss->fd));
        desc = unexpected_message;
        errCode = SSL_ERROR_RX_UNEXPECTED_HELLO_RETRY_REQUEST;
        goto alert_loser;
    }

    /* There are three situations in which the server must pick
     * TLS 1.3.
     *
     * 1. We received HRR
     * 2. We sent early app data
     * 3. ECH was accepted (checked in MaybeHandleEchSignal)
     *
     * If we offered ECH and the server negotiated a lower version,
     * authenticate to the public name for secure disablement.
     *
     */
    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        if (isHelloRetry || ss->ssl3.hs.helloRetry) {
            /* SSL3_SendAlert() will uncache the SID. */
            desc = illegal_parameter;
            errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
            goto alert_loser;
        }
        if (ss->ssl3.hs.zeroRttState == ssl_0rtt_sent) {
            /* SSL3_SendAlert() will uncache the SID. */
            desc = illegal_parameter;
            errCode = SSL_ERROR_DOWNGRADE_WITH_EARLY_DATA;
            goto alert_loser;
        }
    }

    /* Check that the server negotiated the same version as it did
     * in the first handshake. This isn't really the best place for
     * us to be getting this version number, but it's what we have.
     * (1294697). */
    if (ss->firstHsDone && (ss->version != ss->ssl3.crSpec->version)) {
        desc = protocol_version;
        errCode = SSL_ERROR_UNSUPPORTED_VERSION;
        goto alert_loser;
    }

    if (ss->opt.enableHelloDowngradeCheck) {
        rv = ssl_CheckServerRandom(ss);
        if (rv != SECSuccess) {
            desc = illegal_parameter;
            errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
            goto alert_loser;
        }
    }

    /* Finally, now all the version-related checks have passed. */
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_version;
    /* Update the write cipher spec to match the version. But not after
     * HelloRetryRequest, because cwSpec might be a 0-RTT cipher spec,
     * in which case this is a no-op. */
    if (!ss->firstHsDone && !isHelloRetry) {
        ssl_GetSpecWriteLock(ss);
        ssl_SetSpecVersions(ss, ss->ssl3.cwSpec);
        ssl_ReleaseSpecWriteLock(ss);
    }

    /* Check that the session ID is as expected. */
    if (!ssl_CheckServerSessionIdCorrectness(ss, &sidBytes)) {
        desc = illegal_parameter;
        errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
        goto alert_loser;
    }

    /* Only initialize hashes if this isn't a Hello Retry. */
    rv = ssl_ClientSetCipherSuite(ss, ss->version, cipher,
                                  !isHelloRetry);
    if (rv != SECSuccess) {
        desc = illegal_parameter;
        errCode = PORT_GetError();
        goto alert_loser;
    }

    dtls_ReceivedFirstMessageInFlight(ss);

    if (isHelloRetry) {
        rv = tls13_HandleHelloRetryRequest(ss, savedMsg, savedLength);
        if (rv != SECSuccess) {
            goto loser;
        }
        return SECSuccess;
    }

    rv = ssl3_HandleParsedExtensions(ss, ssl_hs_server_hello);
    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.remoteExtensions);
    if (rv != SECSuccess) {
        goto alert_loser;
    }

    rv = ssl_HashHandshakeMessage(ss, ssl_hs_server_hello,
                                  savedMsg, savedLength);
    if (rv != SECSuccess) {
        goto loser;
    }

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        rv = tls13_HandleServerHelloPart2(ss, savedMsg, savedLength);
        if (rv != SECSuccess) {
            errCode = PORT_GetError();
            goto loser;
        }
    } else {
        rv = ssl3_HandleServerHelloPart2(ss, &sidBytes, &errCode);
        if (rv != SECSuccess)
            goto loser;
    }

    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_ech;
    return SECSuccess;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);

loser:
    /* Clean up the temporary pointer to the handshake buffer. */
    ss->xtnData.signedCertTimestamps.len = 0;
    ssl_MapLowLevelError(errCode);
    return SECFailure;
}

static SECStatus
ssl3_UnwrapMasterSecretClient(sslSocket *ss, sslSessionID *sid, PK11SymKey **ms)
{
    PK11SlotInfo *slot;
    PK11SymKey *wrapKey;
    CK_FLAGS keyFlags = 0;
    SECItem wrappedMS = {
        siBuffer,
        sid->u.ssl3.keys.wrapped_master_secret,
        sid->u.ssl3.keys.wrapped_master_secret_len
    };

    /* unwrap master secret */
    slot = SECMOD_LookupSlot(sid->u.ssl3.masterModuleID,
                             sid->u.ssl3.masterSlotID);
    if (slot == NULL) {
        return SECFailure;
    }
    if (!PK11_IsPresent(slot)) {
        PK11_FreeSlot(slot);
        return SECFailure;
    }
    wrapKey = PK11_GetWrapKey(slot, sid->u.ssl3.masterWrapIndex,
                              sid->u.ssl3.masterWrapMech,
                              sid->u.ssl3.masterWrapSeries,
                              ss->pkcs11PinArg);
    PK11_FreeSlot(slot);
    if (wrapKey == NULL) {
        return SECFailure;
    }

    if (ss->version > SSL_LIBRARY_VERSION_3_0) { /* isTLS */
        keyFlags = CKF_SIGN | CKF_VERIFY;
    }

    *ms = PK11_UnwrapSymKeyWithFlags(wrapKey, sid->u.ssl3.masterWrapMech,
                                     NULL, &wrappedMS, CKM_SSL3_MASTER_KEY_DERIVE,
                                     CKA_DERIVE, SSL3_MASTER_SECRET_LENGTH, keyFlags);
    PK11_FreeSymKey(wrapKey);
    if (!*ms) {
        return SECFailure;
    }
    return SECSuccess;
}

static SECStatus
ssl3_HandleServerHelloPart2(sslSocket *ss, const SECItem *sidBytes,
                            int *retErrCode)
{
    SSL3AlertDescription desc = handshake_failure;
    int errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
    SECStatus rv;
    PRBool sid_match;
    sslSessionID *sid = ss->sec.ci.sid;

    if ((ss->opt.requireSafeNegotiation ||
         (ss->firstHsDone && (ss->peerRequestedProtection ||
                              ss->opt.enableRenegotiation ==
                                  SSL_RENEGOTIATE_REQUIRES_XTN))) &&
        !ssl3_ExtensionNegotiated(ss, ssl_renegotiation_info_xtn)) {
        desc = handshake_failure;
        errCode = ss->firstHsDone ? SSL_ERROR_RENEGOTIATION_NOT_ALLOWED
                                  : SSL_ERROR_UNSAFE_NEGOTIATION;
        goto alert_loser;
    }

    /* Any errors after this point are not "malformed" errors. */
    desc = handshake_failure;

    /* we need to call ssl3_SetupPendingCipherSpec here so we can check the
     * key exchange algorithm. */
    rv = ssl3_SetupBothPendingCipherSpecs(ss);
    if (rv != SECSuccess) {
        goto alert_loser; /* error code is set. */
    }

    /* We may or may not have sent a session id, we may get one back or
     * not and if so it may match the one we sent.
     * Attempt to restore the master secret to see if this is so...
     * Don't consider failure to find a matching SID an error.
     */
    sid_match = (PRBool)(sidBytes->len > 0 &&
                         sidBytes->len ==
                             sid->u.ssl3.sessionIDLength &&
                         !PORT_Memcmp(sid->u.ssl3.sessionID,
                                      sidBytes->data, sidBytes->len));

    if (sid_match) {
        if (sid->version != ss->version ||
            sid->u.ssl3.cipherSuite != ss->ssl3.hs.cipher_suite) {
            errCode = SSL_ERROR_RX_MALFORMED_SERVER_HELLO;
            goto alert_loser;
        }
        do {
            PK11SymKey *masterSecret;

            /* [draft-ietf-tls-session-hash-06; Section 5.3]
             *
             * o  If the original session did not use the "extended_master_secret"
             *    extension but the new ServerHello contains the extension, the
             *    client MUST abort the handshake.
             */
            if (!sid->u.ssl3.keys.extendedMasterSecretUsed &&
                ssl3_ExtensionNegotiated(ss, ssl_extended_master_secret_xtn)) {
                errCode = SSL_ERROR_UNEXPECTED_EXTENDED_MASTER_SECRET;
                goto alert_loser;
            }

            /*
             *   o  If the original session used an extended master secret but the new
             *      ServerHello does not contain the "extended_master_secret"
             *      extension, the client SHOULD abort the handshake.
             *
             * TODO(ekr@rtfm.com): Add option to refuse to resume when EMS is not
             * used at all (bug 1176526).
             */
            if (sid->u.ssl3.keys.extendedMasterSecretUsed &&
                !ssl3_ExtensionNegotiated(ss, ssl_extended_master_secret_xtn)) {
                errCode = SSL_ERROR_MISSING_EXTENDED_MASTER_SECRET;
                goto alert_loser;
            }

            ss->sec.authType = sid->authType;
            ss->sec.authKeyBits = sid->authKeyBits;
            ss->sec.keaType = sid->keaType;
            ss->sec.keaKeyBits = sid->keaKeyBits;
            ss->sec.originalKeaGroup = ssl_LookupNamedGroup(sid->keaGroup);
            ss->sec.signatureScheme = sid->sigScheme;

            rv = ssl3_UnwrapMasterSecretClient(ss, sid, &masterSecret);
            if (rv != SECSuccess) {
                break; /* not considered an error */
            }

            /* Got a Match */
            SSL_AtomicIncrementLong(&ssl3stats.hsh_sid_cache_hits);

            /* If we sent a session ticket, then this is a stateless resume. */
            if (ss->xtnData.sentSessionTicketInClientHello)
                SSL_AtomicIncrementLong(&ssl3stats.hsh_sid_stateless_resumes);

            if (ssl3_ExtensionNegotiated(ss, ssl_session_ticket_xtn))
                ss->ssl3.hs.ws = wait_new_session_ticket;
            else
                ss->ssl3.hs.ws = wait_change_cipher;

            ss->ssl3.hs.isResuming = PR_TRUE;

            /* copy the peer cert from the SID */
            if (sid->peerCert != NULL) {
                ss->sec.peerCert = CERT_DupCertificate(sid->peerCert);
            }

            /* We are re-using the old MS, so no need to derive again. */
            rv = ssl3_InitPendingCipherSpecs(ss, masterSecret, PR_FALSE);
            if (rv != SECSuccess) {
                goto alert_loser; /* err code was set */
            }
            return SECSuccess;
        } while (0);
    }

    if (sid_match)
        SSL_AtomicIncrementLong(&ssl3stats.hsh_sid_cache_not_ok);
    else
        SSL_AtomicIncrementLong(&ssl3stats.hsh_sid_cache_misses);

    /* We tried to resume a 1.3 session but the server negotiated 1.2. */
    if (ss->statelessResume) {
        PORT_Assert(sid->version == SSL_LIBRARY_VERSION_TLS_1_3);
        PORT_Assert(ss->ssl3.hs.currentSecret);

        /* Reset resumption state, only used by 1.3 code. */
        ss->statelessResume = PR_FALSE;

        /* Clear TLS 1.3 early data traffic key. */
        PK11_FreeSymKey(ss->ssl3.hs.currentSecret);
        ss->ssl3.hs.currentSecret = NULL;
    }

    /* throw the old one away */
    sid->u.ssl3.keys.resumable = PR_FALSE;
    ssl_UncacheSessionID(ss);
    ssl_FreeSID(sid);

    /* get a new sid */
    ss->sec.ci.sid = sid = ssl3_NewSessionID(ss, PR_FALSE);
    if (sid == NULL) {
        goto alert_loser; /* memory error is set. */
    }

    sid->version = ss->version;
    sid->u.ssl3.sessionIDLength = sidBytes->len;
    if (sidBytes->len > 0) {
        PORT_Memcpy(sid->u.ssl3.sessionID, sidBytes->data, sidBytes->len);
    }

    sid->u.ssl3.keys.extendedMasterSecretUsed =
        ssl3_ExtensionNegotiated(ss, ssl_extended_master_secret_xtn);

    /* Copy Signed Certificate Timestamps, if any. */
    if (ss->xtnData.signedCertTimestamps.len) {
        rv = SECITEM_CopyItem(NULL, &sid->u.ssl3.signedCertTimestamps,
                              &ss->xtnData.signedCertTimestamps);
        ss->xtnData.signedCertTimestamps.len = 0;
        if (rv != SECSuccess)
            goto loser;
    }

    ss->ssl3.hs.isResuming = PR_FALSE;
    if (ss->ssl3.hs.kea_def->authKeyType != ssl_auth_null) {
        /* All current cipher suites other than those with ssl_auth_null (i.e.,
         * (EC)DH_anon_* suites) require a certificate, so use that signal. */
        ss->ssl3.hs.ws = wait_server_cert;
    } else {
        /* All the remaining cipher suites must be (EC)DH_anon_* and so
         * must be ephemeral. Note, if we ever add PSK this might
         * change. */
        PORT_Assert(ss->ssl3.hs.kea_def->ephemeral);
        ss->ssl3.hs.ws = wait_server_key;
    }
    return SECSuccess;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);

loser:
    *retErrCode = errCode;
    return SECFailure;
}

static SECStatus
ssl_HandleDHServerKeyExchange(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;
    int errCode = SSL_ERROR_RX_MALFORMED_SERVER_KEY_EXCH;
    SSL3AlertDescription desc = illegal_parameter;
    SSLHashType hashAlg;
    PRBool isTLS = ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0;
    SSLSignatureScheme sigScheme;

    SECItem dh_p = { siBuffer, NULL, 0 };
    SECItem dh_g = { siBuffer, NULL, 0 };
    SECItem dh_Ys = { siBuffer, NULL, 0 };
    unsigned dh_p_bits;
    unsigned dh_g_bits;
    PRInt32 minDH = 0;
    PRInt32 optval;

    SSL3Hashes hashes;
    SECItem signature = { siBuffer, NULL, 0 };
    PLArenaPool *arena = NULL;
    SECKEYPublicKey *peerKey = NULL;

    rv = ssl3_ConsumeHandshakeVariable(ss, &dh_p, 2, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* malformed. */
    }
    rv = NSS_OptionGet(NSS_KEY_SIZE_POLICY_FLAGS, &optval);
    if ((rv == SECSuccess) && (optval & NSS_KEY_SIZE_POLICY_SSL_FLAG)) {
        (void)NSS_OptionGet(NSS_DH_MIN_KEY_SIZE, &minDH);
    }

    if (minDH <= 0) {
        minDH = SSL_DH_MIN_P_BITS;
    }
    dh_p_bits = SECKEY_BigIntegerBitLength(&dh_p);
    if (dh_p_bits < (unsigned)minDH) {
        errCode = SSL_ERROR_WEAK_SERVER_EPHEMERAL_DH_KEY;
        goto alert_loser;
    }
    if (dh_p_bits > SSL_MAX_DH_KEY_BITS) {
        errCode = SSL_ERROR_DH_KEY_TOO_LONG;
        goto alert_loser;
    }
    rv = ssl3_ConsumeHandshakeVariable(ss, &dh_g, 2, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* malformed. */
    }
    /* Abort if dh_g is 0, 1, or obviously too big. */
    dh_g_bits = SECKEY_BigIntegerBitLength(&dh_g);
    if (dh_g_bits > dh_p_bits || dh_g_bits <= 1) {
        goto alert_loser;
    }
    if (ss->opt.requireDHENamedGroups) {
        /* If we're doing named groups, make sure it's good. */
        rv = ssl_ValidateDHENamedGroup(ss, &dh_p, &dh_g, NULL, NULL);
        if (rv != SECSuccess) {
            errCode = SSL_ERROR_WEAK_SERVER_EPHEMERAL_DH_KEY;
            goto alert_loser;
        }
    }

    rv = ssl3_ConsumeHandshakeVariable(ss, &dh_Ys, 2, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* malformed. */
    }
    if (!ssl_IsValidDHEShare(&dh_p, &dh_Ys)) {
        errCode = SSL_ERROR_RX_MALFORMED_DHE_KEY_SHARE;
        goto alert_loser;
    }

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_2) {
        rv = ssl_ConsumeSignatureScheme(ss, &b, &length, &sigScheme);
        if (rv != SECSuccess) {
            goto loser; /* alert already sent */
        }
        rv = ssl_CheckSignatureSchemeConsistency(
            ss, sigScheme, &ss->sec.peerCert->subjectPublicKeyInfo);
        if (rv != SECSuccess) {
            goto alert_loser;
        }
        hashAlg = ssl_SignatureSchemeToHashType(sigScheme);
    } else {
        /* Use ssl_hash_none to represent the MD5+SHA1 combo. */
        hashAlg = ssl_hash_none;
        sigScheme = ssl_sig_none;
    }
    rv = ssl3_ConsumeHandshakeVariable(ss, &signature, 2, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* malformed. */
    }
    if (length != 0) {
        if (isTLS) {
            desc = decode_error;
        }
        goto alert_loser; /* malformed. */
    }

    PRINT_BUF(60, (NULL, "Server DH p", dh_p.data, dh_p.len));
    PRINT_BUF(60, (NULL, "Server DH g", dh_g.data, dh_g.len));
    PRINT_BUF(60, (NULL, "Server DH Ys", dh_Ys.data, dh_Ys.len));

    /* failures after this point are not malformed handshakes. */
    /* TLS: send decrypt_error if signature failed. */
    desc = isTLS ? decrypt_error : handshake_failure;

    /*
     * Check to make sure the hash is signed by right guy.
     */
    rv = ssl3_ComputeDHKeyHash(ss, hashAlg, &hashes,
                               dh_p, dh_g, dh_Ys, PR_FALSE /* padY */);
    if (rv != SECSuccess) {
        errCode =
            ssl_MapLowLevelError(SSL_ERROR_SERVER_KEY_EXCHANGE_FAILURE);
        goto alert_loser;
    }
    rv = ssl3_VerifySignedHashes(ss, sigScheme, &hashes, &signature);
    if (rv != SECSuccess) {
        errCode =
            ssl_MapLowLevelError(SSL_ERROR_SERVER_KEY_EXCHANGE_FAILURE);
        goto alert_loser;
    }

    /*
     * we really need to build a new key here because we can no longer
     * ignore calling SECKEY_DestroyPublicKey. Using the key may allocate
     * pkcs11 slots and ID's.
     */
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        errCode = SEC_ERROR_NO_MEMORY;
        goto loser;
    }

    peerKey = PORT_ArenaZNew(arena, SECKEYPublicKey);
    if (peerKey == NULL) {
        errCode = SEC_ERROR_NO_MEMORY;
        goto loser;
    }

    peerKey->arena = arena;
    peerKey->keyType = dhKey;
    peerKey->pkcs11Slot = NULL;
    peerKey->pkcs11ID = CK_INVALID_HANDLE;

    if (SECITEM_CopyItem(arena, &peerKey->u.dh.prime, &dh_p) ||
        SECITEM_CopyItem(arena, &peerKey->u.dh.base, &dh_g) ||
        SECITEM_CopyItem(arena, &peerKey->u.dh.publicValue, &dh_Ys)) {
        errCode = SEC_ERROR_NO_MEMORY;
        goto loser;
    }
    ss->sec.peerKey = peerKey;
    return SECSuccess;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);
loser:
    if (arena) {
        PORT_FreeArena(arena, PR_FALSE);
    }
    PORT_SetError(ssl_MapLowLevelError(errCode));
    return SECFailure;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered a
 * complete ssl3 ServerKeyExchange message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleServerKeyExchange(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;

    SSL_TRC(3, ("%d: SSL3[%d]: handle server_key_exchange handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->ssl3.hs.ws != wait_server_key) {
        SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_SERVER_KEY_EXCH);
        return SECFailure;
    }

    switch (ss->ssl3.hs.kea_def->exchKeyType) {
        case ssl_kea_dh:
            rv = ssl_HandleDHServerKeyExchange(ss, b, length);
            break;

        case ssl_kea_ecdh:
            rv = ssl3_HandleECDHServerKeyExchange(ss, b, length);
            break;

        default:
            SSL3_SendAlert(ss, alert_fatal, handshake_failure);
            PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
            rv = SECFailure;
            break;
    }

    if (rv == SECSuccess) {
        ss->ssl3.hs.ws = wait_cert_request;
    }
    /* All Handle*ServerKeyExchange functions set the error code. */
    return rv;
}

typedef struct dnameNode {
    struct dnameNode *next;
    SECItem name;
} dnameNode;

/*
 * Parse the ca_list structure in a CertificateRequest.
 *
 * Called from:
 * ssl3_HandleCertificateRequest
 * tls13_HandleCertificateRequest
 */
SECStatus
ssl3_ParseCertificateRequestCAs(sslSocket *ss, PRUint8 **b, PRUint32 *length,
                                CERTDistNames *ca_list)
{
    PRUint32 remaining;
    int nnames = 0;
    dnameNode *node;
    SECStatus rv;
    int i;

    rv = ssl3_ConsumeHandshakeNumber(ss, &remaining, 2, b, length);
    if (rv != SECSuccess)
        return SECFailure; /* malformed, alert has been sent */

    if (remaining > *length)
        goto alert_loser;

    ca_list->head = node = PORT_ArenaZNew(ca_list->arena, dnameNode);
    if (node == NULL)
        goto no_mem;

    while (remaining > 0) {
        PRUint32 len;

        if (remaining < 2)
            goto alert_loser; /* malformed */

        rv = ssl3_ConsumeHandshakeNumber(ss, &len, 2, b, length);
        if (rv != SECSuccess)
            return SECFailure; /* malformed, alert has been sent */
        if (len == 0 || remaining < len + 2)
            goto alert_loser; /* malformed */

        remaining -= 2;
        if (SECITEM_MakeItem(ca_list->arena, &node->name, *b, len) != SECSuccess) {
            goto no_mem;
        }
        node->name.len = len;
        *b += len;
        *length -= len;
        remaining -= len;
        nnames++;
        if (remaining <= 0)
            break; /* success */

        node->next = PORT_ArenaZNew(ca_list->arena, dnameNode);
        node = node->next;
        if (node == NULL)
            goto no_mem;
    }

    ca_list->nnames = nnames;
    ca_list->names = PORT_ArenaNewArray(ca_list->arena, SECItem, nnames);
    if (nnames > 0 && ca_list->names == NULL)
        goto no_mem;

    for (i = 0, node = (dnameNode *)ca_list->head;
         i < nnames;
         i++, node = node->next) {
        ca_list->names[i] = node->name;
    }

    return SECSuccess;

no_mem:
    return SECFailure;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal,
                         ss->version < SSL_LIBRARY_VERSION_TLS_1_0 ? illegal_parameter
                                                                   : decode_error);
    PORT_SetError(SSL_ERROR_RX_MALFORMED_CERT_REQUEST);
    return SECFailure;
}

SECStatus
ssl_ParseSignatureSchemes(const sslSocket *ss, PLArenaPool *arena,
                          SSLSignatureScheme **schemesOut,
                          unsigned int *numSchemesOut,
                          unsigned char **b, unsigned int *len)
{
    SECStatus rv;
    SECItem buf;
    SSLSignatureScheme *schemes = NULL;
    unsigned int numSupported = 0;
    unsigned int numRemaining = 0;
    unsigned int max;

    rv = ssl3_ExtConsumeHandshakeVariable(ss, &buf, 2, b, len);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    /* An odd-length value is invalid. */
    if ((buf.len & 1) != 0) {
        ssl3_ExtSendAlert(ss, alert_fatal, decode_error);
        return SECFailure;
    }

    /* Let the caller decide whether to alert here. */
    if (buf.len == 0) {
        goto done;
    }

    /* Limit the number of schemes we read. */
    numRemaining = buf.len / 2;
    max = PR_MIN(numRemaining, MAX_SIGNATURE_SCHEMES);

    if (arena) {
        schemes = PORT_ArenaZNewArray(arena, SSLSignatureScheme, max);
    } else {
        schemes = PORT_ZNewArray(SSLSignatureScheme, max);
    }
    if (!schemes) {
        ssl3_ExtSendAlert(ss, alert_fatal, internal_error);
        return SECFailure;
    }

    for (; numRemaining && numSupported < MAX_SIGNATURE_SCHEMES; --numRemaining) {
        PRUint32 tmp;
        rv = ssl3_ExtConsumeHandshakeNumber(ss, &tmp, 2, &buf.data, &buf.len);
        if (rv != SECSuccess) {
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        if (ssl_SignatureSchemeValid((SSLSignatureScheme)tmp, SEC_OID_UNKNOWN,
                                     (PRBool)ss->version >= SSL_LIBRARY_VERSION_TLS_1_3)) {
            ;
            schemes[numSupported++] = (SSLSignatureScheme)tmp;
        }
    }

    if (!numSupported) {
        if (!arena) {
            PORT_Free(schemes);
        }
        schemes = NULL;
    }

done:
    *schemesOut = schemes;
    *numSchemesOut = numSupported;
    return SECSuccess;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 Certificate Request message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleCertificateRequest(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    PLArenaPool *arena = NULL;
    PRBool isTLS = PR_FALSE;
    PRBool isTLS12 = PR_FALSE;
    int errCode = SSL_ERROR_RX_MALFORMED_CERT_REQUEST;
    SECStatus rv;
    SSL3AlertDescription desc = illegal_parameter;
    SECItem cert_types = { siBuffer, NULL, 0 };
    SSLSignatureScheme *signatureSchemes = NULL;
    unsigned int signatureSchemeCount = 0;
    CERTDistNames ca_list;

    SSL_TRC(3, ("%d: SSL3[%d]: handle certificate_request handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->ssl3.hs.ws != wait_cert_request) {
        desc = unexpected_message;
        errCode = SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST;
        goto alert_loser;
    }

    PORT_Assert(ss->ssl3.clientCertChain == NULL);
    PORT_Assert(ss->ssl3.clientCertificate == NULL);
    PORT_Assert(ss->ssl3.clientPrivateKey == NULL);

    isTLS = (PRBool)(ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0);
    isTLS12 = (PRBool)(ss->ssl3.prSpec->version >= SSL_LIBRARY_VERSION_TLS_1_2);
    rv = ssl3_ConsumeHandshakeVariable(ss, &cert_types, 1, &b, &length);
    if (rv != SECSuccess)
        goto loser; /* malformed, alert has been sent */

    arena = ca_list.arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL)
        goto no_mem;

    if (isTLS12) {
        rv = ssl_ParseSignatureSchemes(ss, arena,
                                       &signatureSchemes,
                                       &signatureSchemeCount,
                                       &b, &length);
        if (rv != SECSuccess) {
            PORT_SetError(SSL_ERROR_RX_MALFORMED_CERT_REQUEST);
            goto loser; /* malformed, alert has been sent */
        }
        if (signatureSchemeCount == 0) {
            errCode = SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM;
            desc = handshake_failure;
            goto alert_loser;
        }
    }

    rv = ssl3_ParseCertificateRequestCAs(ss, &b, &length, &ca_list);
    if (rv != SECSuccess)
        goto done; /* alert sent in ssl3_ParseCertificateRequestCAs */

    if (length != 0)
        goto alert_loser; /* malformed */

    ss->ssl3.hs.ws = wait_hello_done;

    rv = ssl3_BeginHandleCertificateRequest(ss, signatureSchemes,
                                            signatureSchemeCount, &ca_list);
    if (rv != SECSuccess) {
        PORT_Assert(0);
        errCode = SEC_ERROR_LIBRARY_FAILURE;
        desc = internal_error;
        goto alert_loser;
    }
    goto done;

no_mem:
    rv = SECFailure;
    PORT_SetError(SEC_ERROR_NO_MEMORY);
    goto done;

alert_loser:
    if (isTLS && desc == illegal_parameter)
        desc = decode_error;
    (void)SSL3_SendAlert(ss, alert_fatal, desc);
loser:
    PORT_SetError(errCode);
    rv = SECFailure;
done:
    if (arena != NULL)
        PORT_FreeArena(arena, PR_FALSE);
    return rv;
}

static void
ssl3_ClientAuthCallbackOutcome(sslSocket *ss, SECStatus outcome)
{
    SECStatus rv;
    switch (outcome) {
        case SECSuccess:
            /* check what the callback function returned */
            if ((!ss->ssl3.clientCertificate) || (!ss->ssl3.clientPrivateKey)) {
                /* we are missing either the key or cert */
                goto send_no_certificate;
            }
            /* Setting ssl3.clientCertChain non-NULL will cause
             * ssl3_HandleServerHelloDone to call SendCertificate.
             */
            ss->ssl3.clientCertChain = CERT_CertChainFromCert(
                ss->ssl3.clientCertificate,
                certUsageSSLClient, PR_FALSE);
            if (ss->ssl3.clientCertChain == NULL) {
                goto send_no_certificate;
            }
            if (ss->ssl3.hs.hashType == handshake_hash_record ||
                ss->ssl3.hs.hashType == handshake_hash_single) {
                rv = ssl_PickClientSignatureScheme(ss,
                                                   ss->ssl3.clientCertificate,
                                                   ss->ssl3.clientPrivateKey,
                                                   ss->ssl3.hs.clientAuthSignatureSchemes,
                                                   ss->ssl3.hs.clientAuthSignatureSchemesLen,
                                                   &ss->ssl3.hs.signatureScheme);
                if (rv != SECSuccess) {
                    /* This should only happen if our schemes changed or
                     * if an RSA-PSS cert was selected, but the token
                     * does not support PSS schemes.
                     */
                    goto send_no_certificate;
                }
            }
            break;

        case SECFailure:
        default:
        send_no_certificate:
            CERT_DestroyCertificate(ss->ssl3.clientCertificate);
            SECKEY_DestroyPrivateKey(ss->ssl3.clientPrivateKey);
            ss->ssl3.clientCertificate = NULL;
            ss->ssl3.clientPrivateKey = NULL;
            if (ss->ssl3.clientCertChain) {
                CERT_DestroyCertificateList(ss->ssl3.clientCertChain);
                ss->ssl3.clientCertChain = NULL;
            }

            if (ss->version > SSL_LIBRARY_VERSION_3_0) {
                ss->ssl3.sendEmptyCert = PR_TRUE;
            } else {
                (void)SSL3_SendAlert(ss, alert_warning, no_certificate);
            }
            break;
    }

    /* Release the cached parameters */
    PORT_Free(ss->ssl3.hs.clientAuthSignatureSchemes);
    ss->ssl3.hs.clientAuthSignatureSchemes = NULL;
    ss->ssl3.hs.clientAuthSignatureSchemesLen = 0;
}

SECStatus
ssl3_BeginHandleCertificateRequest(sslSocket *ss,
                                   const SSLSignatureScheme *signatureSchemes,
                                   unsigned int signatureSchemeCount,
                                   CERTDistNames *ca_list)
{
    SECStatus rv;

    PR_ASSERT(!ss->ssl3.hs.clientCertificatePending);

    /* Should not send a client cert when (non-GREASE) ECH is rejected. */
    if (ss->ssl3.hs.echHpkeCtx && !ss->ssl3.hs.echAccepted) {
        PORT_Assert(ssl3_ExtensionAdvertised(ss, ssl_tls13_encrypted_client_hello_xtn));
        rv = SECFailure;
    } else if (ss->getClientAuthData != NULL) {
        PORT_Assert(signatureSchemes || !signatureSchemeCount);
        PORT_Assert((ss->ssl3.hs.preliminaryInfo & ssl_preinfo_all) ==
                    ssl_preinfo_all);
        PORT_Assert(ss->ssl3.clientPrivateKey == NULL);
        PORT_Assert(ss->ssl3.clientCertificate == NULL);
        PORT_Assert(ss->ssl3.clientCertChain == NULL);

        /* Previously cached parameters should be empty */
        PORT_Assert(ss->ssl3.hs.clientAuthSignatureSchemes == NULL);
        PORT_Assert(ss->ssl3.hs.clientAuthSignatureSchemesLen == 0);
        /*
         * Peer signatures are only available while in the context of
         * of a getClientAuthData callback. It is required for proper
         * functioning of SSL_CertIsUsable and SSL_FilterClientCertListBySocket
         * Calling these functions outside the context of a getClientAuthData
         * callback will result in no filtering.*/

        ss->ssl3.hs.clientAuthSignatureSchemes = PORT_ZNewArray(SSLSignatureScheme, signatureSchemeCount);
        if (signatureSchemes) {
            PORT_Memcpy(ss->ssl3.hs.clientAuthSignatureSchemes, signatureSchemes, signatureSchemeCount * sizeof(SSLSignatureScheme));
        }
        ss->ssl3.hs.clientAuthSignatureSchemesLen = signatureSchemeCount;

        rv = (SECStatus)(*ss->getClientAuthData)(ss->getClientAuthDataArg,
                                                 ss->fd, ca_list,
                                                 &ss->ssl3.clientCertificate,
                                                 &ss->ssl3.clientPrivateKey);
    } else {
        rv = SECFailure; /* force it to send a no_certificate alert */
    }

    if (rv == SECWouldBlock) {
        /* getClientAuthData needs more time (e.g. for user interaction) */

        /* The out parameters should not have changed. */
        PORT_Assert(ss->ssl3.clientCertificate == NULL);
        PORT_Assert(ss->ssl3.clientPrivateKey == NULL);

        /* Mark the handshake as blocked */
        ss->ssl3.hs.clientCertificatePending = PR_TRUE;

        rv = SECSuccess;
    } else {
        /* getClientAuthData returned SECSuccess or SECFailure immediately, handle accordingly */
        ssl3_ClientAuthCallbackOutcome(ss, rv);
        rv = SECSuccess;
    }
    return rv;
}

/* Invoked by the application when client certificate selection is complete */
SECStatus
ssl3_ClientCertCallbackComplete(sslSocket *ss, SECStatus outcome, SECKEYPrivateKey *clientPrivateKey, CERTCertificate *clientCertificate)
{
    PORT_Assert(ss->ssl3.hs.clientCertificatePending);
    ss->ssl3.hs.clientCertificatePending = PR_FALSE;

    ss->ssl3.clientCertificate = clientCertificate;
    ss->ssl3.clientPrivateKey = clientPrivateKey;

    ssl3_ClientAuthCallbackOutcome(ss, outcome);

    /* Continue the handshake */
    PORT_Assert(ss->ssl3.hs.restartTarget);
    if (!ss->ssl3.hs.restartTarget) {
        FATAL_ERROR(ss, PR_INVALID_STATE_ERROR, internal_error);
        return SECFailure;
    }
    sslRestartTarget target = ss->ssl3.hs.restartTarget;
    ss->ssl3.hs.restartTarget = NULL;
    return target(ss);
}

static SECStatus
ssl3_CheckFalseStart(sslSocket *ss)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(!ss->ssl3.hs.authCertificatePending);
    PORT_Assert(!ss->ssl3.hs.canFalseStart);

    if (!ss->canFalseStartCallback) {
        SSL_TRC(3, ("%d: SSL[%d]: no false start callback so no false start",
                    SSL_GETPID(), ss->fd));
    } else {
        SECStatus rv;

        rv = ssl_CheckServerRandom(ss);
        if (rv != SECSuccess) {
            SSL_TRC(3, ("%d: SSL[%d]: no false start due to possible downgrade",
                        SSL_GETPID(), ss->fd));
            goto no_false_start;
        }

        /* An attacker can control the selected ciphersuite so we only wish to
         * do False Start in the case that the selected ciphersuite is
         * sufficiently strong that the attack can gain no advantage.
         * Therefore we always require an 80-bit cipher. */
        ssl_GetSpecReadLock(ss);
        PRBool weakCipher = ss->ssl3.cwSpec->cipherDef->secret_key_size < 10;
        ssl_ReleaseSpecReadLock(ss);
        if (weakCipher) {
            SSL_TRC(3, ("%d: SSL[%d]: no false start due to weak cipher",
                        SSL_GETPID(), ss->fd));
            goto no_false_start;
        }

        if (ssl3_ExtensionAdvertised(ss, ssl_tls13_encrypted_client_hello_xtn)) {
            SSL_TRC(3, ("%d: SSL[%d]: no false start due to lower version after ECH",
                        SSL_GETPID(), ss->fd));
            goto no_false_start;
        }

        PORT_Assert((ss->ssl3.hs.preliminaryInfo & ssl_preinfo_all) ==
                    ssl_preinfo_all);
        rv = (ss->canFalseStartCallback)(ss->fd,
                                         ss->canFalseStartCallbackData,
                                         &ss->ssl3.hs.canFalseStart);
        if (rv == SECSuccess) {
            SSL_TRC(3, ("%d: SSL[%d]: false start callback returned %s",
                        SSL_GETPID(), ss->fd,
                        ss->ssl3.hs.canFalseStart ? "TRUE"
                                                  : "FALSE"));
        } else {
            SSL_TRC(3, ("%d: SSL[%d]: false start callback failed (%s)",
                        SSL_GETPID(), ss->fd,
                        PR_ErrorToName(PR_GetError())));
        }
        return rv;
    }

no_false_start:
    ss->ssl3.hs.canFalseStart = PR_FALSE;
    return SECSuccess;
}

PRBool
ssl3_WaitingForServerSecondRound(sslSocket *ss)
{
    PRBool result;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    switch (ss->ssl3.hs.ws) {
        case wait_new_session_ticket:
        case wait_change_cipher:
        case wait_finished:
            result = PR_TRUE;
            break;
        default:
            result = PR_FALSE;
            break;
    }

    return result;
}

static SECStatus ssl3_SendClientSecondRound(sslSocket *ss);

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 Server Hello Done message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleServerHelloDone(sslSocket *ss)
{
    SECStatus rv;
    SSL3WaitState ws = ss->ssl3.hs.ws;

    SSL_TRC(3, ("%d: SSL3[%d]: handle server_hello_done handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    /* Skipping CertificateRequest is always permitted. */
    if (ws != wait_hello_done &&
        ws != wait_cert_request) {
        SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HELLO_DONE);
        return SECFailure;
    }

    rv = ssl3_SendClientSecondRound(ss);

    return rv;
}

/* Called from ssl3_HandleServerHelloDone and ssl3_AuthCertificateComplete.
 *
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_SendClientSecondRound(sslSocket *ss)
{
    SECStatus rv;
    PRBool sendClientCert;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    sendClientCert = !ss->ssl3.sendEmptyCert &&
                     ss->ssl3.clientCertChain != NULL &&
                     ss->ssl3.clientPrivateKey != NULL;

    /* We must wait for the server's certificate to be authenticated before
     * sending the client certificate in order to disclosing the client
     * certificate to an attacker that does not have a valid cert for the
     * domain we are connecting to.
     *
     * During the initial handshake on a connection, we never send/receive
     * application data until we have authenticated the server's certificate;
     * i.e. we have fully authenticated the handshake before using the cipher
     * specs agreed upon for that handshake. During a renegotiation, we may
     * continue sending and receiving application data during the handshake
     * interleaved with the handshake records. If we were to send the client's
     * second round for a renegotiation before the server's certificate was
     * authenticated, then the application data sent/received after this point
     * would be using cipher spec that hadn't been authenticated. By waiting
     * until the server's certificate has been authenticated during
     * renegotiations, we ensure that renegotiations have the same property
     * as initial handshakes; i.e. we have fully authenticated the handshake
     * before using the cipher specs agreed upon for that handshake for
     * application data.
     */
    if (ss->ssl3.hs.restartTarget) {
        PR_NOT_REACHED("unexpected ss->ssl3.hs.restartTarget");
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }
    /* Check whether waiting for client certificate selection OR
       waiting on server certificate verification AND
       going to send client cert */
    if ((ss->ssl3.hs.clientCertificatePending) ||
        (ss->ssl3.hs.authCertificatePending && (sendClientCert || ss->ssl3.sendEmptyCert || ss->firstHsDone))) {
        SSL_TRC(3, ("%d: SSL3[%p]: deferring ssl3_SendClientSecondRound because"
                    " certificate authentication is still pending.",
                    SSL_GETPID(), ss->fd));
        ss->ssl3.hs.restartTarget = ssl3_SendClientSecondRound;
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return SECFailure;
    }

    ssl_GetXmitBufLock(ss); /*******************************/

    if (ss->ssl3.sendEmptyCert) {
        ss->ssl3.sendEmptyCert = PR_FALSE;
        rv = ssl3_SendEmptyCertificate(ss);
        /* Don't send verify */
        if (rv != SECSuccess) {
            goto loser; /* error code is set. */
        }
    } else if (sendClientCert) {
        rv = ssl3_SendCertificate(ss);
        if (rv != SECSuccess) {
            goto loser; /* error code is set. */
        }
    }

    rv = ssl3_SendClientKeyExchange(ss);
    if (rv != SECSuccess) {
        goto loser; /* err is set. */
    }

    if (sendClientCert) {
        rv = ssl3_SendCertificateVerify(ss, ss->ssl3.clientPrivateKey);
        SECKEY_DestroyPrivateKey(ss->ssl3.clientPrivateKey);
        ss->ssl3.clientPrivateKey = NULL;
        if (rv != SECSuccess) {
            goto loser; /* err is set. */
        }
    }

    rv = ssl3_SendChangeCipherSpecs(ss);
    if (rv != SECSuccess) {
        goto loser; /* err code was set. */
    }

    /* This must be done after we've set ss->ssl3.cwSpec in
     * ssl3_SendChangeCipherSpecs because SSL_GetChannelInfo uses information
     * from cwSpec. This must be done before we call ssl3_CheckFalseStart
     * because the false start callback (if any) may need the information from
     * the functions that depend on this being set.
     */
    ss->enoughFirstHsDone = PR_TRUE;

    if (!ss->firstHsDone) {
        if (ss->opt.enableFalseStart) {
            if (!ss->ssl3.hs.authCertificatePending) {
                /* When we fix bug 589047, we will need to know whether we are
                 * false starting before we try to flush the client second
                 * round to the network. With that in mind, we purposefully
                 * call ssl3_CheckFalseStart before calling ssl3_SendFinished,
                 * which includes a call to ssl3_FlushHandshake, so that
                 * no application develops a reliance on such flushing being
                 * done before its false start callback is called.
                 */
                ssl_ReleaseXmitBufLock(ss);
                rv = ssl3_CheckFalseStart(ss);
                ssl_GetXmitBufLock(ss);
                if (rv != SECSuccess) {
                    goto loser;
                }
            } else {
                /* The certificate authentication and the server's Finished
                 * message are racing each other. If the certificate
                 * authentication wins, then we will try to false start in
                 * ssl3_AuthCertificateComplete.
                 */
                SSL_TRC(3, ("%d: SSL3[%p]: deferring false start check because"
                            " certificate authentication is still pending.",
                            SSL_GETPID(), ss->fd));
            }
        }
    }

    rv = ssl3_SendFinished(ss, 0);
    if (rv != SECSuccess) {
        goto loser; /* err code was set. */
    }

    ssl_ReleaseXmitBufLock(ss); /*******************************/

    if (ssl3_ExtensionNegotiated(ss, ssl_session_ticket_xtn))
        ss->ssl3.hs.ws = wait_new_session_ticket;
    else
        ss->ssl3.hs.ws = wait_change_cipher;

    PORT_Assert(ssl3_WaitingForServerSecondRound(ss));

    return SECSuccess;

loser:
    ssl_ReleaseXmitBufLock(ss);
    return rv;
}

/*
 * Routines used by servers
 */
static SECStatus
ssl3_SendHelloRequest(sslSocket *ss)
{
    SECStatus rv;

    SSL_TRC(3, ("%d: SSL3[%d]: send hello_request handshake", SSL_GETPID(),
                ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_hello_request, 0);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake */
    }
    rv = ssl3_FlushHandshake(ss, 0);
    if (rv != SECSuccess) {
        return rv; /* error code set by ssl3_FlushHandshake */
    }
    ss->ssl3.hs.ws = wait_client_hello;
    return SECSuccess;
}

/*
 * Called from:
 *  ssl3_HandleClientHello()
 */
static SECComparison
ssl3_ServerNameCompare(const SECItem *name1, const SECItem *name2)
{
    if (!name1 != !name2) {
        return SECLessThan;
    }
    if (!name1) {
        return SECEqual;
    }
    if (name1->type != name2->type) {
        return SECLessThan;
    }
    return SECITEM_CompareItem(name1, name2);
}

/* Sets memory error when returning NULL.
 * Called from:
 *  ssl3_SendClientHello()
 *  ssl3_HandleServerHello()
 *  ssl3_HandleClientHello()
 *  ssl3_HandleV2ClientHello()
 */
sslSessionID *
ssl3_NewSessionID(sslSocket *ss, PRBool is_server)
{
    sslSessionID *sid;

    sid = PORT_ZNew(sslSessionID);
    if (sid == NULL)
        return sid;

    if (is_server) {
        const SECItem *srvName;
        SECStatus rv = SECSuccess;

        ssl_GetSpecReadLock(ss); /********************************/
        srvName = &ss->ssl3.hs.srvVirtName;
        if (srvName->len && srvName->data) {
            rv = SECITEM_CopyItem(NULL, &sid->u.ssl3.srvName, srvName);
        }
        ssl_ReleaseSpecReadLock(ss); /************************************/
        if (rv != SECSuccess) {
            PORT_Free(sid);
            return NULL;
        }
    }
    sid->peerID = (ss->peerID == NULL) ? NULL : PORT_Strdup(ss->peerID);
    sid->urlSvrName = (ss->url == NULL) ? NULL : PORT_Strdup(ss->url);
    sid->addr = ss->sec.ci.peer;
    sid->port = ss->sec.ci.port;
    sid->references = 1;
    sid->cached = never_cached;
    sid->version = ss->version;
    sid->sigScheme = ssl_sig_none;

    sid->u.ssl3.keys.resumable = PR_TRUE;
    sid->u.ssl3.policy = SSL_ALLOWED;
    sid->u.ssl3.keys.extendedMasterSecretUsed = PR_FALSE;

    if (is_server) {
        SECStatus rv;
        int pid = SSL_GETPID();

        sid->u.ssl3.sessionIDLength = SSL3_SESSIONID_BYTES;
        sid->u.ssl3.sessionID[0] = (pid >> 8) & 0xff;
        sid->u.ssl3.sessionID[1] = pid & 0xff;
        rv = PK11_GenerateRandom(sid->u.ssl3.sessionID + 2,
                                 SSL3_SESSIONID_BYTES - 2);
        if (rv != SECSuccess) {
            ssl_FreeSID(sid);
            ssl_MapLowLevelError(SSL_ERROR_GENERATE_RANDOM_FAILURE);
            return NULL;
        }
    }
    return sid;
}

/* Called from:  ssl3_HandleClientHello, ssl3_HandleV2ClientHello */
static SECStatus
ssl3_SendServerHelloSequence(sslSocket *ss)
{
    const ssl3KEADef *kea_def;
    SECStatus rv;

    SSL_TRC(3, ("%d: SSL3[%d]: begin send server_hello sequence",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));

    rv = ssl3_SendServerHello(ss);
    if (rv != SECSuccess) {
        return rv; /* err code is set. */
    }
    rv = ssl3_SendCertificate(ss);
    if (rv != SECSuccess) {
        return rv; /* error code is set. */
    }
    rv = ssl3_SendCertificateStatus(ss);
    if (rv != SECSuccess) {
        return rv; /* error code is set. */
    }
    /* We have to do this after the call to ssl3_SendServerHello,
     * because kea_def is set up by ssl3_SendServerHello().
     */
    kea_def = ss->ssl3.hs.kea_def;

    if (kea_def->ephemeral) {
        rv = ssl3_SendServerKeyExchange(ss);
        if (rv != SECSuccess) {
            return rv; /* err code was set. */
        }
    }

    if (ss->opt.requestCertificate) {
        rv = ssl3_SendCertificateRequest(ss);
        if (rv != SECSuccess) {
            return rv; /* err code is set. */
        }
    }
    rv = ssl3_SendServerHelloDone(ss);
    if (rv != SECSuccess) {
        return rv; /* err code is set. */
    }

    ss->ssl3.hs.ws = (ss->opt.requestCertificate) ? wait_client_cert
                                                  : wait_client_key;
    return SECSuccess;
}

/* An empty TLS Renegotiation Info (RI) extension */
static const PRUint8 emptyRIext[5] = { 0xff, 0x01, 0x00, 0x01, 0x00 };

static PRBool
ssl3_KEASupportsTickets(const ssl3KEADef *kea_def)
{
    if (kea_def->signKeyType == dsaKey) {
        /* TODO: Fix session tickets for DSS. The server code rejects the
         * session ticket received from the client. Bug 1174677 */
        return PR_FALSE;
    }
    return PR_TRUE;
}

static PRBool
ssl3_PeerSupportsCipherSuite(const SECItem *peerSuites, uint16_t suite)
{
    for (unsigned int i = 0; i + 1 < peerSuites->len; i += 2) {
        PRUint16 suite_i = (peerSuites->data[i] << 8) | peerSuites->data[i + 1];
        if (suite_i == suite) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

SECStatus
ssl3_NegotiateCipherSuiteInner(sslSocket *ss, const SECItem *suites,
                               PRUint16 version, PRUint16 *suitep)
{
    unsigned int i;
    SSLVersionRange vrange = { version, version };

    /* If we negotiated an External PSK and that PSK has a ciphersuite
     * configured, we need to constrain our choice. If the client does
     * not support it, negotiate a certificate auth suite and fall back.
     */
    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
        ss->xtnData.selectedPsk &&
        ss->xtnData.selectedPsk->type == ssl_psk_external &&
        ss->xtnData.selectedPsk->zeroRttSuite != TLS_NULL_WITH_NULL_NULL) {
        PRUint16 pskSuite = ss->xtnData.selectedPsk->zeroRttSuite;
        ssl3CipherSuiteCfg *pskSuiteCfg = ssl_LookupCipherSuiteCfgMutable(pskSuite,
                                                                          ss->cipherSuites);
        if (ssl3_config_match(pskSuiteCfg, ss->ssl3.policy, &vrange, ss) &&
            ssl3_PeerSupportsCipherSuite(suites, pskSuite)) {
            *suitep = pskSuite;
            return SECSuccess;
        }
    }

    for (i = 0; i < ssl_V3_SUITES_IMPLEMENTED; i++) {
        ssl3CipherSuiteCfg *suite = &ss->cipherSuites[i];
        if (!ssl3_config_match(suite, ss->ssl3.policy, &vrange, ss)) {
            continue;
        }
        if (!ssl3_PeerSupportsCipherSuite(suites, suite->cipher_suite)) {
            continue;
        }
        *suitep = suite->cipher_suite;
        return SECSuccess;
    }
    PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
    return SECFailure;
}

/* Select a cipher suite.
**
** NOTE: This suite selection algorithm should be the same as the one in
** ssl3_HandleV2ClientHello().
**
** If TLS 1.0 is enabled, we could handle the case where the client
** offered TLS 1.1 but offered only export cipher suites by choosing TLS
** 1.0 and selecting one of those export cipher suites. However, a secure
** TLS 1.1 client should not have export cipher suites enabled at all,
** and a TLS 1.1 client should definitely not be offering *only* export
** cipher suites. Therefore, we refuse to negotiate export cipher suites
** with any client that indicates support for TLS 1.1 or higher when we
** (the server) have TLS 1.1 support enabled.
*/
SECStatus
ssl3_NegotiateCipherSuite(sslSocket *ss, const SECItem *suites,
                          PRBool initHashes)
{
    PRUint16 selected;
    SECStatus rv;

    /* Ensure that only valid cipher suites are enabled. */
    if (ssl3_config_match_init(ss) == 0) {
        /* No configured cipher is both supported by PK11 and allowed.
         * This is a configuration error, so report handshake failure.*/
        FATAL_ERROR(ss, PORT_GetError(), handshake_failure);
        return SECFailure;
    }

    rv = ssl3_NegotiateCipherSuiteInner(ss, suites, ss->version, &selected);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    ss->ssl3.hs.cipher_suite = selected;
    return ssl3_SetupCipherSuite(ss, initHashes);
}

/*
 * Call the SNI config hook.
 *
 * Called from:
 *   ssl3_HandleClientHello
 *   tls13_HandleClientHelloPart2
 */
SECStatus
ssl3_ServerCallSNICallback(sslSocket *ss)
{
    int errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
    SSL3AlertDescription desc = illegal_parameter;
    int ret = 0;

#ifdef SSL_SNI_ALLOW_NAME_CHANGE_2HS
#error("No longer allowed to set SSL_SNI_ALLOW_NAME_CHANGE_2HS")
#endif
    if (!ssl3_ExtensionNegotiated(ss, ssl_server_name_xtn)) {
        if (ss->firstHsDone) {
            /* Check that we don't have the name is current spec
             * if this extension was not negotiated on the 2d hs. */
            PRBool passed = PR_TRUE;
            ssl_GetSpecReadLock(ss); /*******************************/
            if (ss->ssl3.hs.srvVirtName.data) {
                passed = PR_FALSE;
            }
            ssl_ReleaseSpecReadLock(ss); /***************************/
            if (!passed) {
                errCode = SSL_ERROR_UNRECOGNIZED_NAME_ALERT;
                desc = handshake_failure;
                goto alert_loser;
            }
        }
        return SECSuccess;
    }

    if (ss->sniSocketConfig)
        do { /* not a loop */
            PORT_Assert((ss->ssl3.hs.preliminaryInfo & ssl_preinfo_all) ==
                        ssl_preinfo_all);

            ret = SSL_SNI_SEND_ALERT;
            /* If extension is negotiated, the len of names should > 0. */
            if (ss->xtnData.sniNameArrSize) {
                /* Calling client callback to reconfigure the socket. */
                ret = (SECStatus)(*ss->sniSocketConfig)(ss->fd,
                                                        ss->xtnData.sniNameArr,
                                                        ss->xtnData.sniNameArrSize,
                                                        ss->sniSocketConfigArg);
            }
            if (ret <= SSL_SNI_SEND_ALERT) {
                /* Application does not know the name or was not able to
                 * properly reconfigure the socket. */
                errCode = SSL_ERROR_UNRECOGNIZED_NAME_ALERT;
                desc = unrecognized_name;
                break;
            } else if (ret == SSL_SNI_CURRENT_CONFIG_IS_USED) {
                SECStatus rv = SECSuccess;
                SECItem pwsNameBuf = { 0, NULL, 0 };
                SECItem *pwsName = &pwsNameBuf;
                SECItem *cwsName;

                ssl_GetSpecWriteLock(ss); /*******************************/
                cwsName = &ss->ssl3.hs.srvVirtName;
                /* not allow name change on the 2d HS */
                if (ss->firstHsDone) {
                    if (ssl3_ServerNameCompare(pwsName, cwsName)) {
                        ssl_ReleaseSpecWriteLock(ss); /******************/
                        errCode = SSL_ERROR_UNRECOGNIZED_NAME_ALERT;
                        desc = handshake_failure;
                        ret = SSL_SNI_SEND_ALERT;
                        break;
                    }
                }
                if (pwsName->data) {
                    SECITEM_FreeItem(pwsName, PR_FALSE);
                }
                if (cwsName->data) {
                    rv = SECITEM_CopyItem(NULL, pwsName, cwsName);
                }
                ssl_ReleaseSpecWriteLock(ss); /**************************/
                if (rv != SECSuccess) {
                    errCode = SSL_ERROR_INTERNAL_ERROR_ALERT;
                    desc = internal_error;
                    ret = SSL_SNI_SEND_ALERT;
                    break;
                }
            } else if ((unsigned int)ret < ss->xtnData.sniNameArrSize) {
                /* Application has configured new socket info. Lets check it
                 * and save the name. */
                SECStatus rv;
                SECItem *name = &ss->xtnData.sniNameArr[ret];
                SECItem *pwsName;

                /* get rid of the old name and save the newly picked. */
                /* This code is protected by ssl3HandshakeLock. */
                ssl_GetSpecWriteLock(ss); /*******************************/
                /* not allow name change on the 2d HS */
                if (ss->firstHsDone) {
                    SECItem *cwsName = &ss->ssl3.hs.srvVirtName;
                    if (ssl3_ServerNameCompare(name, cwsName)) {
                        ssl_ReleaseSpecWriteLock(ss); /******************/
                        errCode = SSL_ERROR_UNRECOGNIZED_NAME_ALERT;
                        desc = handshake_failure;
                        ret = SSL_SNI_SEND_ALERT;
                        break;
                    }
                }
                pwsName = &ss->ssl3.hs.srvVirtName;
                if (pwsName->data) {
                    SECITEM_FreeItem(pwsName, PR_FALSE);
                }
                rv = SECITEM_CopyItem(NULL, pwsName, name);
                ssl_ReleaseSpecWriteLock(ss); /***************************/
                if (rv != SECSuccess) {
                    errCode = SSL_ERROR_INTERNAL_ERROR_ALERT;
                    desc = internal_error;
                    ret = SSL_SNI_SEND_ALERT;
                    break;
                }
                /* Need to tell the client that application has picked
                 * the name from the offered list and reconfigured the socket.
                 */
                ssl3_RegisterExtensionSender(ss, &ss->xtnData, ssl_server_name_xtn,
                                             ssl_SendEmptyExtension);
            } else {
                /* Callback returned index outside of the boundary. */
                PORT_Assert((unsigned int)ret < ss->xtnData.sniNameArrSize);
                errCode = SSL_ERROR_INTERNAL_ERROR_ALERT;
                desc = internal_error;
                ret = SSL_SNI_SEND_ALERT;
                break;
            }
        } while (0);
    ssl3_FreeSniNameArray(&ss->xtnData);
    if (ret <= SSL_SNI_SEND_ALERT) {
        /* desc and errCode should be set. */
        goto alert_loser;
    }

    return SECSuccess;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);
    PORT_SetError(errCode);
    return SECFailure;
}

SECStatus
ssl3_SelectServerCert(sslSocket *ss)
{
    const ssl3KEADef *kea_def = ss->ssl3.hs.kea_def;
    PRCList *cursor;
    SECStatus rv;

    /* If the client didn't include the supported groups extension, assume just
     * P-256 support and disable all the other ECDHE groups.  This also affects
     * ECDHE group selection, but this function is called first. */
    if (!ssl3_ExtensionNegotiated(ss, ssl_supported_groups_xtn)) {
        unsigned int i;
        for (i = 0; i < SSL_NAMED_GROUP_COUNT; ++i) {
            if (ss->namedGroupPreferences[i] &&
                ss->namedGroupPreferences[i]->keaType == ssl_kea_ecdh &&
                ss->namedGroupPreferences[i]->name != ssl_grp_ec_secp256r1) {
                ss->namedGroupPreferences[i] = NULL;
            }
        }
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
        if (kea_def->authKeyType == ssl_auth_rsa_sign) {
            /* We consider PSS certificates here as well for TLS 1.2. */
            if (!SSL_CERT_IS(cert, ssl_auth_rsa_sign) &&
                (!SSL_CERT_IS(cert, ssl_auth_rsa_pss) ||
                 ss->version < SSL_LIBRARY_VERSION_TLS_1_2)) {
                continue;
            }
        } else {
            if (!SSL_CERT_IS(cert, kea_def->authKeyType)) {
                continue;
            }
            if (SSL_CERT_IS_EC(cert) &&
                !ssl_NamedGroupEnabled(ss, cert->namedCurve)) {
                continue;
            }
        }

        /* Found one. */
        ss->sec.serverCert = cert;
        ss->sec.authKeyBits = cert->serverKeyBits;

        /* Don't pick a signature scheme if we aren't going to use it. */
        if (kea_def->signKeyType == nullKey) {
            ss->sec.authType = kea_def->authKeyType;
            return SECSuccess;
        }

        rv = ssl3_PickServerSignatureScheme(ss);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        ss->sec.authType =
            ssl_SignatureSchemeToAuthType(ss->ssl3.hs.signatureScheme);
        return SECSuccess;
    }

    PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
    return SECFailure;
}

static SECStatus
ssl_GenerateServerRandom(sslSocket *ss)
{
    SECStatus rv;
    PRUint8 *downgradeSentinel;

    rv = ssl3_GetNewRandom(ss->ssl3.hs.server_random);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->version == ss->vrange.max) {
        return SECSuccess;
    }

    /*
     * [RFC 8446 Section 4.1.3].
     *
     * TLS 1.3 servers which negotiate TLS 1.2 or below in response to a
     * ClientHello MUST set the last 8 bytes of their Random value specially in
     * their ServerHello.
     *
     * If negotiating TLS 1.2, TLS 1.3 servers MUST set the last 8 bytes of
     * their Random value to the bytes:
     *
     *   44 4F 57 4E 47 52 44 01
     *
     * If negotiating TLS 1.1 or below, TLS 1.3 servers MUST, and TLS 1.2
     * servers SHOULD, set the last 8 bytes of their ServerHello.Random value to
     * the bytes:
     *
     *   44 4F 57 4E 47 52 44 00
     */
    downgradeSentinel =
        ss->ssl3.hs.server_random +
        SSL3_RANDOM_LENGTH - sizeof(tls12_downgrade_random);
    if (ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_2) {
        switch (ss->version) {
            case SSL_LIBRARY_VERSION_TLS_1_2:
                /* vrange.max > 1.2, since we didn't early exit above. */
                PORT_Memcpy(downgradeSentinel,
                            tls12_downgrade_random, sizeof(tls12_downgrade_random));
                break;
            case SSL_LIBRARY_VERSION_TLS_1_1:
            case SSL_LIBRARY_VERSION_TLS_1_0:
                PORT_Memcpy(downgradeSentinel,
                            tls1_downgrade_random, sizeof(tls1_downgrade_random));
                break;
            default:
                /* Do not change random. */
                break;
        }
    }

    return SECSuccess;
}

SECStatus
ssl3_HandleClientHelloPreamble(sslSocket *ss, PRUint8 **b, PRUint32 *length, SECItem *sidBytes,
                               SECItem *cookieBytes, SECItem *suites, SECItem *comps)
{
    SECStatus rv;
    PRUint32 tmp;
    rv = ssl3_ConsumeHandshakeNumber(ss, &tmp, 2, b, length);
    if (rv != SECSuccess) {
        return SECFailure; /* malformed, alert already sent */
    }

    /* Translate the version. */
    if (IS_DTLS(ss)) {
        ss->clientHelloVersion = dtls_DTLSVersionToTLSVersion((SSL3ProtocolVersion)tmp);
    } else {
        ss->clientHelloVersion = (SSL3ProtocolVersion)tmp;
    }

    /* Grab the client random data. */
    rv = ssl3_ConsumeHandshake(
        ss, ss->ssl3.hs.client_random, SSL3_RANDOM_LENGTH, b, length);
    if (rv != SECSuccess) {
        return SECFailure; /* malformed */
    }

    /* Grab the client's SID, if present. */
    rv = ssl3_ConsumeHandshakeVariable(ss, sidBytes, 1, b, length);
    /* Check that the SID has the format: opaque legacy_session_id<0..32>, as
     * specified in RFC8446, Section 4.1.2. */
    if (rv != SECSuccess || sidBytes->len > SSL3_SESSIONID_BYTES) {
        return SECFailure; /* malformed */
    }

    /* Grab the client's cookie, if present. It is checked after version negotiation. */
    if (IS_DTLS(ss)) {
        rv = ssl3_ConsumeHandshakeVariable(ss, cookieBytes, 1, b, length);
        if (rv != SECSuccess) {
            return SECFailure; /* malformed */
        }
    }

    /* Grab the list of cipher suites. */
    rv = ssl3_ConsumeHandshakeVariable(ss, suites, 2, b, length);
    if (rv != SECSuccess) {
        return SECFailure; /* malformed */
    }

    /* Grab the list of compression methods. */
    rv = ssl3_ConsumeHandshakeVariable(ss, comps, 1, b, length);
    if (rv != SECSuccess) {
        return SECFailure; /* malformed */
    }
    return SECSuccess;
}

static SECStatus
ssl3_ValidatePreambleWithVersion(sslSocket *ss, const SECItem *sidBytes, const SECItem *comps,
                                 const SECItem *cookieBytes)
{
    SECStatus rv;
    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        if (sidBytes->len > 0 && !IS_DTLS(ss)) {
            SECITEM_FreeItem(&ss->ssl3.hs.fakeSid, PR_FALSE);
            rv = SECITEM_CopyItem(NULL, &ss->ssl3.hs.fakeSid, sidBytes);
            if (rv != SECSuccess) {
                FATAL_ERROR(ss, PORT_GetError(), internal_error);
                return SECFailure;
            }
        }

        /* TLS 1.3 requires that compression include only null. */
        if (comps->len != 1 || comps->data[0] != ssl_compression_null) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, illegal_parameter);
            return SECFailure;
        }

        /* receivedCcs is only valid if we sent an HRR. */
        if (ss->ssl3.hs.receivedCcs && !ss->ssl3.hs.helloRetry) {
            FATAL_ERROR(ss, SSL_ERROR_RX_UNEXPECTED_CHANGE_CIPHER, unexpected_message);
            return SECFailure;
        }

        /* A DTLS 1.3-only client MUST set the legacy_cookie field to zero length.
         * If a DTLS 1.3 ClientHello is received with any other value in this field,
         * the server MUST abort the handshake with an "illegal_parameter" alert. */
        if (IS_DTLS(ss) && cookieBytes->len != 0) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, illegal_parameter);
            return SECFailure;
        }
    } else {
        /* ECH not possible here. */
        ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_ech;

        /* HRR and ECH are TLS1.3-only. We ignore the Cookie extension here. */
        if (ss->ssl3.hs.helloRetry) {
            FATAL_ERROR(ss, SSL_ERROR_UNSUPPORTED_VERSION, protocol_version);
            return SECFailure;
        }

        /* receivedCcs is only valid if we sent an HRR. */
        if (ss->ssl3.hs.receivedCcs) {
            FATAL_ERROR(ss, SSL_ERROR_RX_UNEXPECTED_CHANGE_CIPHER, unexpected_message);
            return SECFailure;
        }

        /* TLS versions prior to 1.3 must include null somewhere. */
        if (comps->len < 1 ||
            !memchr(comps->data, ssl_compression_null, comps->len)) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, illegal_parameter);
            return SECFailure;
        }

        /* We never send cookies in DTLS 1.2. */
        if (IS_DTLS(ss) && cookieBytes->len != 0) {
            FATAL_ERROR(ss, SSL_ERROR_RX_MALFORMED_CLIENT_HELLO, illegal_parameter);
            return SECFailure;
        }
    }

    return SECSuccess;
}

/* Called from ssl3_HandleHandshakeMessage() when it has deciphered a complete
 * ssl3 Client Hello message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleClientHello(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    sslSessionID *sid = NULL;
    unsigned int i;
    SECStatus rv;
    PRUint32 extensionLength;
    int errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
    SSL3AlertDescription desc = illegal_parameter;
    SSL3AlertLevel level = alert_fatal;
    TLSExtension *versionExtension;
    SECItem sidBytes = { siBuffer, NULL, 0 };
    SECItem cookieBytes = { siBuffer, NULL, 0 };
    SECItem suites = { siBuffer, NULL, 0 };
    SECItem comps = { siBuffer, NULL, 0 };
    SECItem *echInner = NULL;
    PRBool isTLS13;
    const PRUint8 *savedMsg = b;
    const PRUint32 savedLen = length;

    SSL_TRC(3, ("%d: SSL3[%d]: handle client_hello handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    ss->ssl3.hs.preliminaryInfo = 0;

    if (!ss->sec.isServer ||
        (ss->ssl3.hs.ws != wait_client_hello &&
         ss->ssl3.hs.ws != idle_handshake)) {
        desc = unexpected_message;
        errCode = SSL_ERROR_RX_UNEXPECTED_CLIENT_HELLO;
        goto alert_loser;
    }
    if (ss->ssl3.hs.ws == idle_handshake) {
        /* Refuse re-handshake when we have already negotiated TLS 1.3. */
        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
            desc = unexpected_message;
            errCode = SSL_ERROR_RENEGOTIATION_NOT_ALLOWED;
            goto alert_loser;
        }
        if (ss->opt.enableRenegotiation == SSL_RENEGOTIATE_NEVER) {
            desc = no_renegotiation;
            level = alert_warning;
            errCode = SSL_ERROR_RENEGOTIATION_NOT_ALLOWED;
            goto alert_loser;
        }
    }

    /* We should always be in a fresh state. */
    SSL_ASSERT_HASHES_EMPTY(ss);

    /* Get peer name of client */
    rv = ssl_GetPeerInfo(ss);
    if (rv != SECSuccess) {
        return rv; /* error code is set. */
    }

    /* We might be starting session renegotiation in which case we should
     * clear previous state.
     */
    ssl3_ResetExtensionData(&ss->xtnData, ss);
    ss->statelessResume = PR_FALSE;

    if (IS_DTLS(ss)) {
        dtls_RehandshakeCleanup(ss);
    }

    rv = ssl3_HandleClientHelloPreamble(ss, &b, &length, &sidBytes,
                                        &cookieBytes, &suites, &comps);
    if (rv != SECSuccess) {
        goto loser; /* malformed */
    }

    /* Handle TLS hello extensions for SSL3 & TLS. We do not know if
     * we are restarting a previous session until extensions have been
     * parsed, since we might have received a SessionTicket extension.
     * Note: we allow extensions even when negotiating SSL3 for the sake
     * of interoperability (and backwards compatibility).
     */
    if (length) {
        /* Get length of hello extensions */
        rv = ssl3_ConsumeHandshakeNumber(ss, &extensionLength, 2, &b, &length);
        if (rv != SECSuccess) {
            goto loser; /* alert already sent */
        }
        if (extensionLength != length) {
            errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
            desc = decode_error;
            goto alert_loser;
        }

        rv = ssl3_ParseExtensions(ss, &b, &length);
        if (rv != SECSuccess) {
            goto loser; /* malformed */
        }
    }

    versionExtension = ssl3_FindExtension(ss, ssl_tls13_supported_versions_xtn);
    if (versionExtension) {
        rv = tls13_NegotiateVersion(ss, versionExtension);
        if (rv != SECSuccess) {
            errCode = PORT_GetError();
            desc = (errCode == SSL_ERROR_UNSUPPORTED_VERSION) ? protocol_version : illegal_parameter;
            goto alert_loser;
        }
    } else {
        /* The PR_MIN here ensures that we never negotiate 1.3 if the
         * peer didn't offer "supported_versions". */
        rv = ssl3_NegotiateVersion(ss,
                                   PR_MIN(ss->clientHelloVersion,
                                          SSL_LIBRARY_VERSION_TLS_1_2),
                                   PR_TRUE);
        /* Send protocol version alert if the ClientHello.legacy_version is not
         * supported by the server.
         *
         * If the "supported_versions" extension is absent and the server only
         * supports versions greater than ClientHello.legacy_version, the
         * server MUST abort the handshake with a "protocol_version" alert
         * [RFC8446, Appendix D.2]. */
        if (rv != SECSuccess) {
            desc = protocol_version;
            errCode = SSL_ERROR_UNSUPPORTED_VERSION;
            goto alert_loser;
        }
    }
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_version;

    /* Update the write spec to match the selected version. */
    if (!ss->firstHsDone) {
        ssl_GetSpecWriteLock(ss);
        ssl_SetSpecVersions(ss, ss->ssl3.cwSpec);
        ssl_ReleaseSpecWriteLock(ss);
    }

    isTLS13 = ss->version >= SSL_LIBRARY_VERSION_TLS_1_3;
    if (isTLS13) {
        if (ss->firstHsDone) {
            desc = unexpected_message;
            errCode = SSL_ERROR_RENEGOTIATION_NOT_ALLOWED;
            goto alert_loser;
        }

        /* If there is a cookie, then this is a second ClientHello (TLS 1.3). */
        if (ssl3_FindExtension(ss, ssl_tls13_cookie_xtn)) {
            ss->ssl3.hs.helloRetry = PR_TRUE;
        }

        rv = tls13_MaybeHandleEch(ss, savedMsg, savedLen, &sidBytes,
                                  &comps, &cookieBytes, &suites, &echInner);
        if (rv != SECSuccess) {
            errCode = PORT_GetError();
            goto loser; /* code set, alert sent. */
        }
    }

    rv = ssl3_ValidatePreambleWithVersion(ss, &sidBytes, &comps, &cookieBytes);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        goto loser; /* code set, alert sent. */
    }

    /* Now parse the rest of the extensions. */
    rv = ssl3_HandleParsedExtensions(ss, ssl_hs_client_hello);
    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.remoteExtensions);
    if (rv != SECSuccess) {
        if (PORT_GetError() == SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM) {
            errCode = SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM;
        }
        goto loser; /* malformed */
    }

    /* If the ClientHello version is less than our maximum version, check for a
     * TLS_FALLBACK_SCSV and reject the connection if found. */
    if (ss->vrange.max > ss->version) {
        for (i = 0; i + 1 < suites.len; i += 2) {
            PRUint16 suite_i = (suites.data[i] << 8) | suites.data[i + 1];
            if (suite_i != TLS_FALLBACK_SCSV)
                continue;
            desc = inappropriate_fallback;
            errCode = SSL_ERROR_INAPPROPRIATE_FALLBACK_ALERT;
            goto alert_loser;
        }
    }

    if (!ssl3_ExtensionNegotiated(ss, ssl_renegotiation_info_xtn)) {
        /* If we didn't receive an RI extension, look for the SCSV,
         * and if found, treat it just like an empty RI extension
         * by processing a local copy of an empty RI extension.
         */
        for (i = 0; i + 1 < suites.len; i += 2) {
            PRUint16 suite_i = (suites.data[i] << 8) | suites.data[i + 1];
            if (suite_i == TLS_EMPTY_RENEGOTIATION_INFO_SCSV) {
                PRUint8 *b2 = (PRUint8 *)emptyRIext;
                PRUint32 L2 = sizeof emptyRIext;
                (void)ssl3_HandleExtensions(ss, &b2, &L2, ssl_hs_client_hello);
                break;
            }
        }
    }

    /* The check for renegotiation in TLS 1.3 is earlier. */
    if (!isTLS13) {
        if (ss->firstHsDone &&
            (ss->opt.enableRenegotiation == SSL_RENEGOTIATE_REQUIRES_XTN ||
             ss->opt.enableRenegotiation == SSL_RENEGOTIATE_TRANSITIONAL) &&
            !ssl3_ExtensionNegotiated(ss, ssl_renegotiation_info_xtn)) {
            desc = no_renegotiation;
            level = alert_warning;
            errCode = SSL_ERROR_RENEGOTIATION_NOT_ALLOWED;
            goto alert_loser;
        }
        if ((ss->opt.requireSafeNegotiation ||
             (ss->firstHsDone && ss->peerRequestedProtection)) &&
            !ssl3_ExtensionNegotiated(ss, ssl_renegotiation_info_xtn)) {
            desc = handshake_failure;
            errCode = SSL_ERROR_UNSAFE_NEGOTIATION;
            goto alert_loser;
        }
    }

    /* We do stateful resumes only if we are in TLS < 1.3 and
     * either of the following conditions are satisfied:
     * (1) the client does not support the session ticket extension, or
     * (2) the client support the session ticket extension, but sent an
     * empty ticket.
     */
    if (!isTLS13 &&
        (!ssl3_ExtensionNegotiated(ss, ssl_session_ticket_xtn) ||
         ss->xtnData.emptySessionTicket)) {
        if (sidBytes.len > 0 && !ss->opt.noCache) {
            SSL_TRC(7, ("%d: SSL3[%d]: server, lookup client session-id for 0x%08x%08x%08x%08x",
                        SSL_GETPID(), ss->fd, ss->sec.ci.peer.pr_s6_addr32[0],
                        ss->sec.ci.peer.pr_s6_addr32[1],
                        ss->sec.ci.peer.pr_s6_addr32[2],
                        ss->sec.ci.peer.pr_s6_addr32[3]));
            if (ssl_sid_lookup) {
                sid = (*ssl_sid_lookup)(ssl_Time(ss), &ss->sec.ci.peer,
                                        sidBytes.data, sidBytes.len, ss->dbHandle);
            } else {
                errCode = SSL_ERROR_SERVER_CACHE_NOT_CONFIGURED;
                goto loser;
            }
        }
    } else if (ss->statelessResume) {
        /* Fill in the client's session ID if doing a stateless resume.
         * (When doing stateless resumes, server echos client's SessionID.)
         * This branch also handles TLS 1.3 resumption-PSK.
         */
        sid = ss->sec.ci.sid;
        PORT_Assert(sid != NULL); /* Should have already been filled in.*/

        if (sidBytes.len > 0 && sidBytes.len <= SSL3_SESSIONID_BYTES) {
            sid->u.ssl3.sessionIDLength = sidBytes.len;
            PORT_Memcpy(sid->u.ssl3.sessionID, sidBytes.data,
                        sidBytes.len);
            sid->u.ssl3.sessionIDLength = sidBytes.len;
        } else {
            sid->u.ssl3.sessionIDLength = 0;
        }
        ss->sec.ci.sid = NULL;
    }

    /* Free a potentially leftover session ID from a previous handshake. */
    if (ss->sec.ci.sid) {
        ssl_FreeSID(ss->sec.ci.sid);
        ss->sec.ci.sid = NULL;
    }

    if (sid != NULL) {
        /* We've found a session cache entry for this client.
         * Now, if we're going to require a client-auth cert,
         * and we don't already have this client's cert in the session cache,
         * and this is the first handshake on this connection (not a redo),
         * then drop this old cache entry and start a new session.
         */
        if ((sid->peerCert == NULL) && ss->opt.requestCertificate &&
            ((ss->opt.requireCertificate == SSL_REQUIRE_ALWAYS) ||
             (ss->opt.requireCertificate == SSL_REQUIRE_NO_ERROR) ||
             ((ss->opt.requireCertificate == SSL_REQUIRE_FIRST_HANDSHAKE) &&
              !ss->firstHsDone))) {

            SSL_AtomicIncrementLong(&ssl3stats.hch_sid_cache_not_ok);
            ssl_FreeSID(sid);
            sid = NULL;
            ss->statelessResume = PR_FALSE;
        }
    }

    if (IS_DTLS(ss)) {
        ssl3_DisableNonDTLSSuites(ss);
        dtls_ReceivedFirstMessageInFlight(ss);
    }

    if (isTLS13) {
        rv = tls13_HandleClientHelloPart2(ss, &suites, sid,
                                          ss->ssl3.hs.echAccepted ? echInner->data : savedMsg,
                                          ss->ssl3.hs.echAccepted ? echInner->len : savedLen);
        SECITEM_FreeItem(echInner, PR_TRUE);
        echInner = NULL;
    } else {
        rv = ssl3_HandleClientHelloPart2(ss, &suites, sid,
                                         savedMsg, savedLen);
    }
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        goto loser;
    }
    return SECSuccess;

alert_loser:
    (void)SSL3_SendAlert(ss, level, desc);
/* FALLTHRU */
loser:
    SECITEM_FreeItem(echInner, PR_TRUE);
    PORT_SetError(errCode);
    return SECFailure;
}

/* unwrap helper function to handle the case where the wrapKey doesn't wind
 * up in the correct token for the master secret */
PK11SymKey *
ssl_unwrapSymKey(PK11SymKey *wrapKey,
                 CK_MECHANISM_TYPE wrapType, SECItem *param,
                 SECItem *wrappedKey,
                 CK_MECHANISM_TYPE target, CK_ATTRIBUTE_TYPE operation,
                 int keySize, CK_FLAGS keyFlags, void *pinArg)
{
    PK11SymKey *unwrappedKey;

    /* unwrap the master secret. */
    unwrappedKey = PK11_UnwrapSymKeyWithFlags(wrapKey, wrapType, param,
                                              wrappedKey, target, operation, keySize,
                                              keyFlags);
    if (!unwrappedKey) {
        PK11SlotInfo *targetSlot = PK11_GetBestSlot(target, pinArg);
        PK11SymKey *newWrapKey;

        /* it's possible that we failed to unwrap because the wrapKey is in
         * a slot that can't handle target. Move the wrapKey to a slot that
         * can handle this mechanism and retry the operation */
        if (targetSlot == NULL) {
            return NULL;
        }
        newWrapKey = PK11_MoveSymKey(targetSlot, CKA_UNWRAP, 0,
                                     PR_FALSE, wrapKey);
        PK11_FreeSlot(targetSlot);
        if (newWrapKey == NULL) {
            return NULL;
        }
        unwrappedKey = PK11_UnwrapSymKeyWithFlags(newWrapKey, wrapType, param,
                                                  wrappedKey, target, operation, keySize,
                                                  keyFlags);
        PK11_FreeSymKey(newWrapKey);
    }
    return unwrappedKey;
}

static SECStatus
ssl3_UnwrapMasterSecretServer(sslSocket *ss, sslSessionID *sid, PK11SymKey **ms)
{
    PK11SymKey *wrapKey;
    CK_FLAGS keyFlags = 0;
    SECItem wrappedMS = {
        siBuffer,
        sid->u.ssl3.keys.wrapped_master_secret,
        sid->u.ssl3.keys.wrapped_master_secret_len
    };

    wrapKey = ssl3_GetWrappingKey(ss, NULL, sid->u.ssl3.masterWrapMech,
                                  ss->pkcs11PinArg);
    if (!wrapKey) {
        return SECFailure;
    }

    if (ss->version > SSL_LIBRARY_VERSION_3_0) { /* isTLS */
        keyFlags = CKF_SIGN | CKF_VERIFY;
    }

    *ms = ssl_unwrapSymKey(wrapKey, sid->u.ssl3.masterWrapMech, NULL,
                           &wrappedMS, CKM_SSL3_MASTER_KEY_DERIVE,
                           CKA_DERIVE, SSL3_MASTER_SECRET_LENGTH,
                           keyFlags, ss->pkcs11PinArg);
    PK11_FreeSymKey(wrapKey);
    if (!*ms) {
        SSL_TRC(10, ("%d: SSL3[%d]: server wrapping key found, but couldn't unwrap MasterSecret. wrapMech=0x%0lx",
                     SSL_GETPID(), ss->fd, sid->u.ssl3.masterWrapMech));
        return SECFailure;
    }
    return SECSuccess;
}

static SECStatus
ssl3_HandleClientHelloPart2(sslSocket *ss,
                            SECItem *suites,
                            sslSessionID *sid,
                            const PRUint8 *msg,
                            unsigned int len)
{
    PRBool haveXmitBufLock = PR_FALSE;
    int errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
    SSL3AlertDescription desc = illegal_parameter;
    SECStatus rv;
    unsigned int i;
    unsigned int j;

    rv = ssl_HashHandshakeMessage(ss, ssl_hs_client_hello, msg, len);
    if (rv != SECSuccess) {
        errCode = SEC_ERROR_LIBRARY_FAILURE;
        desc = internal_error;
        goto alert_loser;
    }

    /* If we already have a session for this client, be sure to pick the same
    ** cipher suite we picked before.  This is not a loop, despite appearances.
    */
    if (sid)
        do {
            ssl3CipherSuiteCfg *suite;
            SSLVersionRange vrange = { ss->version, ss->version };

            suite = ss->cipherSuites;
            /* Find the entry for the cipher suite used in the cached session. */
            for (j = ssl_V3_SUITES_IMPLEMENTED; j > 0; --j, ++suite) {
                if (suite->cipher_suite == sid->u.ssl3.cipherSuite)
                    break;
            }

            if (j == 0)
                break;

            /* Double check that the cached cipher suite is still enabled,
             * implemented, and allowed by policy.  Might have been disabled.
             */
            if (ssl3_config_match_init(ss) == 0) {
                desc = handshake_failure;
                errCode = PORT_GetError();
                goto alert_loser;
            }
            if (!ssl3_config_match(suite, ss->ssl3.policy, &vrange, ss))
                break;

            /* Double check that the cached cipher suite is in the client's
             * list.  If it isn't, fall through and start a new session. */
            for (i = 0; i + 1 < suites->len; i += 2) {
                PRUint16 suite_i = (suites->data[i] << 8) | suites->data[i + 1];
                if (suite_i == suite->cipher_suite) {
                    ss->ssl3.hs.cipher_suite = suite_i;
                    rv = ssl3_SetupCipherSuite(ss, PR_TRUE);
                    if (rv != SECSuccess) {
                        desc = internal_error;
                        errCode = PORT_GetError();
                        goto alert_loser;
                    }

                    goto cipher_found;
                }
            }
        } while (0);
    /* START A NEW SESSION */

    rv = ssl3_NegotiateCipherSuite(ss, suites, PR_TRUE);
    if (rv != SECSuccess) {
        desc = handshake_failure;
        errCode = PORT_GetError();
        goto alert_loser;
    }

cipher_found:
    suites->data = NULL;

    /* If there are any failures while processing the old sid,
     * we don't consider them to be errors.  Instead, We just behave
     * as if the client had sent us no sid to begin with, and make a new one.
     * The exception here is attempts to resume extended_master_secret
     * sessions without the extension, which causes an alert.
     */
    if (sid != NULL)
        do {
            PK11SymKey *masterSecret;

            if (sid->version != ss->version ||
                sid->u.ssl3.cipherSuite != ss->ssl3.hs.cipher_suite) {
                break; /* not an error */
            }

            /* server sids don't remember the server cert we previously sent,
            ** but they do remember the slot we originally used, so we
            ** can locate it again, provided that the current ssl socket
            ** has had its server certs configured the same as the previous one.
            */
            ss->sec.serverCert = ssl_FindServerCert(ss, sid->authType, sid->namedCurve);
            if (!ss->sec.serverCert || !ss->sec.serverCert->serverCert) {
                /* A compatible certificate must not have been configured.  It
                 * might not be the same certificate, but we only find that out
                 * when the ticket fails to decrypt. */
                break;
            }

            /* [draft-ietf-tls-session-hash-06; Section 5.3]
             * o  If the original session did not use the "extended_master_secret"
             *    extension but the new ClientHello contains the extension, then the
             *    server MUST NOT perform the abbreviated handshake.  Instead, it
             *    SHOULD continue with a full handshake (as described in
             *    Section 5.2) to negotiate a new session.
             *
             * o  If the original session used the "extended_master_secret"
             *    extension but the new ClientHello does not contain the extension,
             *    the server MUST abort the abbreviated handshake.
             */
            if (ssl3_ExtensionNegotiated(ss, ssl_extended_master_secret_xtn)) {
                if (!sid->u.ssl3.keys.extendedMasterSecretUsed) {
                    break; /* not an error */
                }
            } else {
                if (sid->u.ssl3.keys.extendedMasterSecretUsed) {
                    /* Note: we do not destroy the session */
                    desc = handshake_failure;
                    errCode = SSL_ERROR_MISSING_EXTENDED_MASTER_SECRET;
                    goto alert_loser;
                }
            }

            if (ss->sec.ci.sid) {
                ssl_UncacheSessionID(ss);
                PORT_Assert(ss->sec.ci.sid != sid); /* should be impossible, but ... */
                if (ss->sec.ci.sid != sid) {
                    ssl_FreeSID(ss->sec.ci.sid);
                }
                ss->sec.ci.sid = NULL;
            }

            /* we need to resurrect the master secret.... */
            rv = ssl3_UnwrapMasterSecretServer(ss, sid, &masterSecret);
            if (rv != SECSuccess) {
                break; /* not an error */
            }

            ss->sec.ci.sid = sid;
            if (sid->peerCert != NULL) {
                ss->sec.peerCert = CERT_DupCertificate(sid->peerCert);
            }

            /*
             * Old SID passed all tests, so resume this old session.
             */
            SSL_AtomicIncrementLong(&ssl3stats.hch_sid_cache_hits);
            if (ss->statelessResume)
                SSL_AtomicIncrementLong(&ssl3stats.hch_sid_stateless_resumes);
            ss->ssl3.hs.isResuming = PR_TRUE;

            ss->sec.authType = sid->authType;
            ss->sec.authKeyBits = sid->authKeyBits;
            ss->sec.keaType = sid->keaType;
            ss->sec.keaKeyBits = sid->keaKeyBits;
            ss->sec.originalKeaGroup = ssl_LookupNamedGroup(sid->keaGroup);
            ss->sec.signatureScheme = sid->sigScheme;

            ss->sec.localCert =
                CERT_DupCertificate(ss->sec.serverCert->serverCert);

            /* Copy cached name in to pending spec */
            if (sid != NULL &&
                sid->version > SSL_LIBRARY_VERSION_3_0 &&
                sid->u.ssl3.srvName.len && sid->u.ssl3.srvName.data) {
                /* Set server name from sid */
                SECItem *sidName = &sid->u.ssl3.srvName;
                SECItem *pwsName = &ss->ssl3.hs.srvVirtName;
                if (pwsName->data) {
                    SECITEM_FreeItem(pwsName, PR_FALSE);
                }
                rv = SECITEM_CopyItem(NULL, pwsName, sidName);
                if (rv != SECSuccess) {
                    errCode = PORT_GetError();
                    desc = internal_error;
                    goto alert_loser;
                }
            }

            /* Clean up sni name array */
            ssl3_FreeSniNameArray(&ss->xtnData);

            ssl_GetXmitBufLock(ss);
            haveXmitBufLock = PR_TRUE;

            rv = ssl3_SendServerHello(ss);
            if (rv != SECSuccess) {
                errCode = PORT_GetError();
                goto loser;
            }

            /* We are re-using the old MS, so no need to derive again. */
            rv = ssl3_InitPendingCipherSpecs(ss, masterSecret, PR_FALSE);
            if (rv != SECSuccess) {
                errCode = PORT_GetError();
                goto loser;
            }

            rv = ssl3_SendChangeCipherSpecs(ss);
            if (rv != SECSuccess) {
                errCode = PORT_GetError();
                goto loser;
            }
            rv = ssl3_SendFinished(ss, 0);
            ss->ssl3.hs.ws = wait_change_cipher;
            if (rv != SECSuccess) {
                errCode = PORT_GetError();
                goto loser;
            }

            if (haveXmitBufLock) {
                ssl_ReleaseXmitBufLock(ss);
            }

            return SECSuccess;
        } while (0);

    if (sid) { /* we had a sid, but it's no longer valid, free it */
        ss->statelessResume = PR_FALSE;
        SSL_AtomicIncrementLong(&ssl3stats.hch_sid_cache_not_ok);
        ssl_UncacheSessionID(ss);
        ssl_FreeSID(sid);
        sid = NULL;
    }
    SSL_AtomicIncrementLong(&ssl3stats.hch_sid_cache_misses);

    /* We only send a session ticket extension if the client supports
     * the extension and we are unable to resume.
     *
     * TODO: send a session ticket if performing a stateful
     * resumption.  (As per RFC4507, a server may issue a session
     * ticket while doing a (stateless or stateful) session resume,
     * but OpenSSL-0.9.8g does not accept session tickets while
     * resuming.)
     */
    if (ssl3_ExtensionNegotiated(ss, ssl_session_ticket_xtn) &&
        ssl3_KEASupportsTickets(ss->ssl3.hs.kea_def)) {
        ssl3_RegisterExtensionSender(ss, &ss->xtnData, ssl_session_ticket_xtn,
                                     ssl_SendEmptyExtension);
    }

    rv = ssl3_ServerCallSNICallback(ss);
    if (rv != SECSuccess) {
        /* The alert has already been sent. */
        errCode = PORT_GetError();
        goto loser;
    }

    rv = ssl3_SelectServerCert(ss);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        desc = handshake_failure;
        goto alert_loser;
    }

    sid = ssl3_NewSessionID(ss, PR_TRUE);
    if (sid == NULL) {
        errCode = PORT_GetError();
        goto loser; /* memory error is set. */
    }
    ss->sec.ci.sid = sid;

    sid->u.ssl3.keys.extendedMasterSecretUsed =
        ssl3_ExtensionNegotiated(ss, ssl_extended_master_secret_xtn);
    ss->ssl3.hs.isResuming = PR_FALSE;

    ssl_GetXmitBufLock(ss);
    rv = ssl3_SendServerHelloSequence(ss);
    ssl_ReleaseXmitBufLock(ss);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        desc = handshake_failure;
        goto alert_loser;
    }

    if (haveXmitBufLock) {
        ssl_ReleaseXmitBufLock(ss);
    }

    return SECSuccess;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);
/* FALLTHRU */
loser:
    if (sid && sid != ss->sec.ci.sid) {
        ssl_UncacheSessionID(ss);
        ssl_FreeSID(sid);
    }

    if (haveXmitBufLock) {
        ssl_ReleaseXmitBufLock(ss);
    }

    PORT_SetError(errCode);
    return SECFailure;
}

/*
 * ssl3_HandleV2ClientHello is used when a V2 formatted hello comes
 * in asking to use the V3 handshake.
 */
SECStatus
ssl3_HandleV2ClientHello(sslSocket *ss, unsigned char *buffer, unsigned int length,
                         PRUint8 padding)
{
    sslSessionID *sid = NULL;
    unsigned char *suites;
    unsigned char *random;
    SSL3ProtocolVersion version;
    SECStatus rv;
    unsigned int i;
    unsigned int j;
    unsigned int sid_length;
    unsigned int suite_length;
    unsigned int rand_length;
    int errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
    SSL3AlertDescription desc = handshake_failure;
    unsigned int total = SSL_HL_CLIENT_HELLO_HBYTES;

    SSL_TRC(3, ("%d: SSL3[%d]: handle v2 client_hello", SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));

    ssl_GetSSL3HandshakeLock(ss);

    version = (buffer[1] << 8) | buffer[2];
    if (version < SSL_LIBRARY_VERSION_3_0) {
        goto loser;
    }

    ssl3_RestartHandshakeHashes(ss);

    if (ss->ssl3.hs.ws != wait_client_hello) {
        desc = unexpected_message;
        errCode = SSL_ERROR_RX_UNEXPECTED_CLIENT_HELLO;
        goto alert_loser;
    }

    total += suite_length = (buffer[3] << 8) | buffer[4];
    total += sid_length = (buffer[5] << 8) | buffer[6];
    total += rand_length = (buffer[7] << 8) | buffer[8];
    total += padding;
    ss->clientHelloVersion = version;

    if (version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        /* [draft-ietf-tls-tls-11; C.3] forbids sending a TLS 1.3
         * ClientHello using the backwards-compatible format. */
        desc = illegal_parameter;
        errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
        goto alert_loser;
    }

    rv = ssl3_NegotiateVersion(ss, version, PR_TRUE);
    if (rv != SECSuccess) {
        /* send back which ever alert client will understand. */
        desc = (version > SSL_LIBRARY_VERSION_3_0) ? protocol_version
                                                   : handshake_failure;
        errCode = SSL_ERROR_UNSUPPORTED_VERSION;
        goto alert_loser;
    }
    /* ECH not possible here. */
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_ech;
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_version;
    if (!ss->firstHsDone) {
        ssl_GetSpecWriteLock(ss);
        ssl_SetSpecVersions(ss, ss->ssl3.cwSpec);
        ssl_ReleaseSpecWriteLock(ss);
    }

    /* if we get a non-zero SID, just ignore it. */
    if (length != total) {
        SSL_DBG(("%d: SSL3[%d]: bad v2 client hello message, len=%d should=%d",
                 SSL_GETPID(), ss->fd, length, total));
        desc = illegal_parameter;
        errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
        goto alert_loser;
    }

    suites = buffer + SSL_HL_CLIENT_HELLO_HBYTES;
    random = suites + suite_length + sid_length;

    if (rand_length < SSL_MIN_CHALLENGE_BYTES ||
        rand_length > SSL_MAX_CHALLENGE_BYTES) {
        desc = illegal_parameter;
        errCode = SSL_ERROR_RX_MALFORMED_CLIENT_HELLO;
        goto alert_loser;
    }

    PORT_Assert(SSL_MAX_CHALLENGE_BYTES == SSL3_RANDOM_LENGTH);

    PORT_Memset(ss->ssl3.hs.client_random, 0, SSL3_RANDOM_LENGTH);
    PORT_Memcpy(&ss->ssl3.hs.client_random[SSL3_RANDOM_LENGTH - rand_length],
                random, rand_length);

    PRINT_BUF(60, (ss, "client random:", ss->ssl3.hs.client_random,
                   SSL3_RANDOM_LENGTH));

    if (ssl3_config_match_init(ss) == 0) {
        errCode = PORT_GetError(); /* error code is already set. */
        goto alert_loser;
    }

    /* Select a cipher suite.
    **
    ** NOTE: This suite selection algorithm should be the same as the one in
    ** ssl3_HandleClientHello().
    */
    for (j = 0; j < ssl_V3_SUITES_IMPLEMENTED; j++) {
        ssl3CipherSuiteCfg *suite = &ss->cipherSuites[j];
        SSLVersionRange vrange = { ss->version, ss->version };
        if (!ssl3_config_match(suite, ss->ssl3.policy, &vrange, ss)) {
            continue;
        }
        for (i = 0; i + 2 < suite_length; i += 3) {
            PRUint32 suite_i = (suites[i] << 16) | (suites[i + 1] << 8) | suites[i + 2];
            if (suite_i == suite->cipher_suite) {
                ss->ssl3.hs.cipher_suite = suite_i;
                rv = ssl3_SetupCipherSuite(ss, PR_TRUE);
                if (rv != SECSuccess) {
                    desc = internal_error;
                    errCode = PORT_GetError();
                    goto alert_loser;
                }
                goto suite_found;
            }
        }
    }
    errCode = SSL_ERROR_NO_CYPHER_OVERLAP;
    goto alert_loser;

suite_found:

    /* If the ClientHello version is less than our maximum version, check for a
     * TLS_FALLBACK_SCSV and reject the connection if found. */
    if (ss->vrange.max > ss->clientHelloVersion) {
        for (i = 0; i + 2 < suite_length; i += 3) {
            PRUint16 suite_i = (suites[i] << 16) | (suites[i + 1] << 8) | suites[i + 2];
            if (suite_i == TLS_FALLBACK_SCSV) {
                desc = inappropriate_fallback;
                errCode = SSL_ERROR_INAPPROPRIATE_FALLBACK_ALERT;
                goto alert_loser;
            }
        }
    }

    /* Look for the SCSV, and if found, treat it just like an empty RI
     * extension by processing a local copy of an empty RI extension.
     */
    for (i = 0; i + 2 < suite_length; i += 3) {
        PRUint32 suite_i = (suites[i] << 16) | (suites[i + 1] << 8) | suites[i + 2];
        if (suite_i == TLS_EMPTY_RENEGOTIATION_INFO_SCSV) {
            PRUint8 *b2 = (PRUint8 *)emptyRIext;
            PRUint32 L2 = sizeof emptyRIext;
            (void)ssl3_HandleExtensions(ss, &b2, &L2, ssl_hs_client_hello);
            break;
        }
    }

    if (ss->opt.requireSafeNegotiation &&
        !ssl3_ExtensionNegotiated(ss, ssl_renegotiation_info_xtn)) {
        desc = handshake_failure;
        errCode = SSL_ERROR_UNSAFE_NEGOTIATION;
        goto alert_loser;
    }

    rv = ssl3_SelectServerCert(ss);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        desc = handshake_failure;
        goto alert_loser;
    }

    /* we don't even search for a cache hit here.  It's just a miss. */
    SSL_AtomicIncrementLong(&ssl3stats.hch_sid_cache_misses);
    sid = ssl3_NewSessionID(ss, PR_TRUE);
    if (sid == NULL) {
        errCode = PORT_GetError();
        goto loser; /* memory error is set. */
    }
    ss->sec.ci.sid = sid;
    /* do not worry about memory leak of sid since it now belongs to ci */

    /* We have to update the handshake hashes before we can send stuff */
    rv = ssl3_UpdateHandshakeHashes(ss, buffer, length);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        goto loser;
    }

    ssl_GetXmitBufLock(ss);
    rv = ssl3_SendServerHelloSequence(ss);
    ssl_ReleaseXmitBufLock(ss);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        goto loser;
    }

    ssl_ReleaseSSL3HandshakeLock(ss);
    return SECSuccess;

alert_loser:
    SSL3_SendAlert(ss, alert_fatal, desc);
loser:
    ssl_ReleaseSSL3HandshakeLock(ss);
    PORT_SetError(errCode);
    return SECFailure;
}

SECStatus
ssl_ConstructServerHello(sslSocket *ss, PRBool helloRetry,
                         const sslBuffer *extensionBuf, sslBuffer *messageBuf)
{
    SECStatus rv;
    SSL3ProtocolVersion version;
    sslSessionID *sid = ss->sec.ci.sid;
    const PRUint8 *random;

    version = PR_MIN(ss->version, SSL_LIBRARY_VERSION_TLS_1_2);
    if (IS_DTLS(ss)) {
        version = dtls_TLSVersionToDTLSVersion(version);
    }
    rv = sslBuffer_AppendNumber(messageBuf, version, 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (helloRetry) {
        random = ssl_hello_retry_random;
    } else {
        rv = ssl_GenerateServerRandom(ss);
        if (rv != SECSuccess) {
            return SECFailure;
        }
        random = ss->ssl3.hs.server_random;
    }
    rv = sslBuffer_Append(messageBuf, random, SSL3_RANDOM_LENGTH);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        if (sid) {
            rv = sslBuffer_AppendVariable(messageBuf, sid->u.ssl3.sessionID,
                                          sid->u.ssl3.sessionIDLength, 1);
        } else {
            rv = sslBuffer_AppendNumber(messageBuf, 0, 1);
        }
    } else {
        rv = sslBuffer_AppendVariable(messageBuf, ss->ssl3.hs.fakeSid.data,
                                      ss->ssl3.hs.fakeSid.len, 1);
    }
    if (rv != SECSuccess) {
        return SECFailure;
    }

    rv = sslBuffer_AppendNumber(messageBuf, ss->ssl3.hs.cipher_suite, 2);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    rv = sslBuffer_AppendNumber(messageBuf, ssl_compression_null, 1);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    if (SSL_BUFFER_LEN(extensionBuf)) {
        /* Directly copy the extensions */
        rv = sslBuffer_AppendBufferVariable(messageBuf, extensionBuf, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    if (ss->xtnData.ech && ss->xtnData.ech->receivedInnerXtn) {
        /* Signal ECH acceptance if we handled handled both CHOuter/CHInner (i.e.
         * in shared mode), or if we received a CHInner in split/backend mode. */
        if (ss->ssl3.hs.echAccepted || ss->opt.enableTls13BackendEch) {
            if (helloRetry) {
                return tls13_WriteServerEchHrrSignal(ss, SSL_BUFFER_BASE(messageBuf),
                                                     SSL_BUFFER_LEN(messageBuf));
            } else {
                return tls13_WriteServerEchSignal(ss, SSL_BUFFER_BASE(messageBuf),
                                                  SSL_BUFFER_LEN(messageBuf));
            }
        }
    }
    return SECSuccess;
}

/* The negotiated version number has been already placed in ss->version.
**
** Called from:  ssl3_HandleClientHello                     (resuming session),
**  ssl3_SendServerHelloSequence <- ssl3_HandleClientHello   (new session),
**  ssl3_SendServerHelloSequence <- ssl3_HandleV2ClientHello (new session)
*/
SECStatus
ssl3_SendServerHello(sslSocket *ss)
{
    SECStatus rv;
    sslBuffer extensionBuf = SSL_BUFFER_EMPTY;
    sslBuffer messageBuf = SSL_BUFFER_EMPTY;

    SSL_TRC(3, ("%d: SSL3[%d]: send server_hello handshake", SSL_GETPID(),
                ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    PORT_Assert(MSB(ss->version) == MSB(SSL_LIBRARY_VERSION_3_0));
    if (MSB(ss->version) != MSB(SSL_LIBRARY_VERSION_3_0)) {
        PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
        return SECFailure;
    }

    rv = ssl_ConstructExtensions(ss, &extensionBuf, ssl_hs_server_hello);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = ssl_ConstructServerHello(ss, PR_FALSE, &extensionBuf, &messageBuf);
    if (rv != SECSuccess) {
        goto loser;
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_server_hello,
                                    SSL_BUFFER_LEN(&messageBuf));
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    rv = ssl3_AppendHandshake(ss, SSL_BUFFER_BASE(&messageBuf),
                              SSL_BUFFER_LEN(&messageBuf));
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
        rv = ssl3_SetupBothPendingCipherSpecs(ss);
        if (rv != SECSuccess) {
            goto loser; /* err set */
        }
    }

    sslBuffer_Clear(&extensionBuf);
    sslBuffer_Clear(&messageBuf);
    return SECSuccess;

loser:
    sslBuffer_Clear(&extensionBuf);
    sslBuffer_Clear(&messageBuf);
    return SECFailure;
}

SECStatus
ssl_CreateDHEKeyPair(const sslNamedGroupDef *groupDef,
                     const ssl3DHParams *params,
                     sslEphemeralKeyPair **keyPair)
{
    SECKEYDHParams dhParam;
    SECKEYPublicKey *pubKey = NULL;   /* Ephemeral DH key */
    SECKEYPrivateKey *privKey = NULL; /* Ephemeral DH key */
    sslEphemeralKeyPair *pair;

    dhParam.prime.data = params->prime.data;
    dhParam.prime.len = params->prime.len;
    dhParam.base.data = params->base.data;
    dhParam.base.len = params->base.len;

    PRINT_BUF(60, (NULL, "Server DH p", dhParam.prime.data,
                   dhParam.prime.len));
    PRINT_BUF(60, (NULL, "Server DH g", dhParam.base.data,
                   dhParam.base.len));

    /* Generate ephemeral DH keypair */
    privKey = SECKEY_CreateDHPrivateKey(&dhParam, &pubKey, NULL);
    if (!privKey || !pubKey) {
        ssl_MapLowLevelError(SEC_ERROR_KEYGEN_FAIL);
        return SECFailure;
    }

    pair = ssl_NewEphemeralKeyPair(groupDef, privKey, pubKey);
    if (!pair) {
        SECKEY_DestroyPrivateKey(privKey);
        SECKEY_DestroyPublicKey(pubKey);

        return SECFailure;
    }

    *keyPair = pair;
    return SECSuccess;
}

static SECStatus
ssl3_SendDHServerKeyExchange(sslSocket *ss)
{
    const ssl3KEADef *kea_def = ss->ssl3.hs.kea_def;
    SECStatus rv = SECFailure;
    int length;
    SECItem signed_hash = { siBuffer, NULL, 0 };
    SSL3Hashes hashes;
    SSLHashType hashAlg;

    const ssl3DHParams *params;
    sslEphemeralKeyPair *keyPair;
    SECKEYPublicKey *pubKey;
    SECKEYPrivateKey *certPrivateKey;
    const sslNamedGroupDef *groupDef;
    /* Do this on the heap, this could be over 2k long. */
    sslBuffer dhBuf = SSL_BUFFER_EMPTY;

    if (kea_def->kea != kea_dhe_dss && kea_def->kea != kea_dhe_rsa) {
        /* TODO: Support DH_anon. It might be sufficient to drop the signature.
                 See bug 1170510. */
        PORT_SetError(SSL_ERROR_SERVER_KEY_EXCHANGE_FAILURE);
        return SECFailure;
    }

    rv = ssl_SelectDHEGroup(ss, &groupDef);
    if (rv == SECFailure) {
        PORT_SetError(SSL_ERROR_NO_CYPHER_OVERLAP);
        return SECFailure;
    }
    ss->sec.keaGroup = groupDef;

    params = ssl_GetDHEParams(groupDef);
    rv = ssl_CreateDHEKeyPair(groupDef, params, &keyPair);
    if (rv == SECFailure) {
        ssl_MapLowLevelError(SEC_ERROR_KEYGEN_FAIL);
        return SECFailure;
    }
    PR_APPEND_LINK(&keyPair->link, &ss->ephemeralKeyPairs);

    if (ss->version == SSL_LIBRARY_VERSION_TLS_1_2) {
        hashAlg = ssl_SignatureSchemeToHashType(ss->ssl3.hs.signatureScheme);
    } else {
        /* Use ssl_hash_none to represent the MD5+SHA1 combo. */
        hashAlg = ssl_hash_none;
    }

    pubKey = keyPair->keys->pubKey;
    PRINT_BUF(50, (ss, "DH public value:",
                   pubKey->u.dh.publicValue.data,
                   pubKey->u.dh.publicValue.len));
    rv = ssl3_ComputeDHKeyHash(ss, hashAlg, &hashes,
                               pubKey->u.dh.prime,
                               pubKey->u.dh.base,
                               pubKey->u.dh.publicValue,
                               PR_TRUE /* padY */);
    if (rv != SECSuccess) {
        ssl_MapLowLevelError(SSL_ERROR_SERVER_KEY_EXCHANGE_FAILURE);
        goto loser;
    }

    certPrivateKey = ss->sec.serverCert->serverKeyPair->privKey;
    rv = ssl3_SignHashes(ss, &hashes, certPrivateKey, &signed_hash);
    if (rv != SECSuccess) {
        goto loser; /* ssl3_SignHashes has set err. */
    }

    length = 2 + pubKey->u.dh.prime.len +
             2 + pubKey->u.dh.base.len +
             2 + pubKey->u.dh.prime.len +
             2 + signed_hash.len;

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_2) {
        length += 2;
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_server_key_exchange, length);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    rv = ssl3_AppendHandshakeVariable(ss, pubKey->u.dh.prime.data,
                                      pubKey->u.dh.prime.len, 2);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    rv = ssl3_AppendHandshakeVariable(ss, pubKey->u.dh.base.data,
                                      pubKey->u.dh.base.len, 2);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    rv = ssl_AppendPaddedDHKeyShare(&dhBuf, pubKey, PR_TRUE);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendPaddedDHKeyShare. */
    }
    rv = ssl3_AppendBufferToHandshake(ss, &dhBuf);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_2) {
        rv = ssl3_AppendHandshakeNumber(ss, ss->ssl3.hs.signatureScheme, 2);
        if (rv != SECSuccess) {
            goto loser; /* err set by AppendHandshake. */
        }
    }

    rv = ssl3_AppendHandshakeVariable(ss, signed_hash.data,
                                      signed_hash.len, 2);
    if (rv != SECSuccess) {
        goto loser; /* err set by AppendHandshake. */
    }

    sslBuffer_Clear(&dhBuf);
    PORT_Free(signed_hash.data);
    return SECSuccess;

loser:
    if (signed_hash.data)
        PORT_Free(signed_hash.data);
    sslBuffer_Clear(&dhBuf);
    return SECFailure;
}

static SECStatus
ssl3_SendServerKeyExchange(sslSocket *ss)
{
    const ssl3KEADef *kea_def = ss->ssl3.hs.kea_def;

    SSL_TRC(3, ("%d: SSL3[%d]: send server_key_exchange handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    switch (kea_def->exchKeyType) {
        case ssl_kea_dh: {
            return ssl3_SendDHServerKeyExchange(ss);
        }

        case ssl_kea_ecdh: {
            return ssl3_SendECDHServerKeyExchange(ss);
        }

        case ssl_kea_rsa:
        case ssl_kea_null:
        default:
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            break;
    }

    return SECFailure;
}

SECStatus
ssl3_EncodeSigAlgs(const sslSocket *ss, PRUint16 minVersion, PRBool forCert,
                   PRBool grease, sslBuffer *buf)
{
    SSLSignatureScheme filtered[MAX_SIGNATURE_SCHEMES] = { 0 };
    unsigned int filteredCount = 0;

    SECStatus rv = ssl3_FilterSigAlgs(ss, minVersion, PR_FALSE, forCert,
                                      PR_ARRAY_SIZE(filtered),
                                      filtered, &filteredCount);
    if (rv != SECSuccess) {
        return SECFailure;
    }
    return ssl3_EncodeFilteredSigAlgs(ss, filtered, filteredCount, grease, buf);
}

SECStatus
ssl3_EncodeFilteredSigAlgs(const sslSocket *ss, const SSLSignatureScheme *schemes,
                           PRUint32 numSchemes, PRBool grease, sslBuffer *buf)
{
    if (!numSchemes) {
        PORT_SetError(SSL_ERROR_NO_SUPPORTED_SIGNATURE_ALGORITHM);
        return SECFailure;
    }

    unsigned int lengthOffset;
    SECStatus rv;

    rv = sslBuffer_Skip(buf, 2, &lengthOffset);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    for (unsigned int i = 0; i < numSchemes; ++i) {
        rv = sslBuffer_AppendNumber(buf, schemes[i], 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    /* GREASE SignatureAlgorithms:
     * A client MAY select one or more GREASE signature algorithm values and
     * advertise them in the "signature_algorithms" or
     * "signature_algorithms_cert" extensions, if sent [RFC8701, Section 3.1].
     *
     * When sending a CertificateRequest in TLS 1.3, a server MAY behave as
     * follows: [...] A server MAY select one or more GREASE signature
     * algorithm values and advertise them in the "signature_algorithms" or
     * "signature_algorithms_cert" extensions, if present
     * [RFC8701, Section 4.1]. */
    if (grease &&
        ((!ss->sec.isServer && ss->vrange.max >= SSL_LIBRARY_VERSION_TLS_1_3) ||
         (ss->sec.isServer && ss->version >= SSL_LIBRARY_VERSION_TLS_1_3))) {
        PRUint16 value;
        if (ss->sec.isServer) {
            rv = tls13_RandomGreaseValue(&value);
            if (rv != SECSuccess) {
                return SECFailure;
            }
        } else {
            value = ss->ssl3.hs.grease->idx[grease_sigalg];
        }
        rv = sslBuffer_AppendNumber(buf, value, 2);
        if (rv != SECSuccess) {
            return SECFailure;
        }
    }

    return sslBuffer_InsertLength(buf, lengthOffset, 2);
}

/*
 * In TLS 1.3 we are permitted to advertise support for PKCS#1
 * schemes. This doesn't affect the signatures in TLS itself, just
 * those on certificates. Not advertising PKCS#1 signatures creates a
 * serious compatibility risk as it excludes many certificate chains
 * that include PKCS#1. Hence, forCert is used to enable advertising
 * PKCS#1 support. Note that we include these in signature_algorithms
 * because we don't yet support signature_algorithms_cert. TLS 1.3
 * requires that PKCS#1 schemes are placed last in the list if they
 * are present. This sorting can be removed once we support
 * signature_algorithms_cert.
 */
SECStatus
ssl3_FilterSigAlgs(const sslSocket *ss, PRUint16 minVersion, PRBool disableRsae,
                   PRBool forCert,
                   unsigned int maxSchemes, SSLSignatureScheme *filteredSchemes,
                   unsigned int *numFilteredSchemes)
{
    PORT_Assert(filteredSchemes);
    PORT_Assert(numFilteredSchemes);
    PORT_Assert(maxSchemes >= ss->ssl3.signatureSchemeCount);
    if (maxSchemes < ss->ssl3.signatureSchemeCount) {
        return SECFailure;
    }

    *numFilteredSchemes = 0;
    PRBool allowUnsortedPkcs1 = forCert && minVersion < SSL_LIBRARY_VERSION_TLS_1_3;
    for (unsigned int i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
        if (disableRsae && ssl_IsRsaeSignatureScheme(ss->ssl3.signatureSchemes[i])) {
            continue;
        }
        if (ssl_SignatureSchemeAccepted(minVersion,
                                        ss->ssl3.signatureSchemes[i],
                                        allowUnsortedPkcs1)) {
            filteredSchemes[(*numFilteredSchemes)++] = ss->ssl3.signatureSchemes[i];
        }
    }
    if (forCert && !allowUnsortedPkcs1) {
        for (unsigned int i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
            if (disableRsae && ssl_IsRsaeSignatureScheme(ss->ssl3.signatureSchemes[i])) {
                continue;
            }
            if (!ssl_SignatureSchemeAccepted(minVersion,
                                             ss->ssl3.signatureSchemes[i],
                                             PR_FALSE) &&
                ssl_SignatureSchemeAccepted(minVersion,
                                            ss->ssl3.signatureSchemes[i],
                                            PR_TRUE)) {
                filteredSchemes[(*numFilteredSchemes)++] = ss->ssl3.signatureSchemes[i];
            }
        }
    }
    return SECSuccess;
}

static SECStatus
ssl3_SendCertificateRequest(sslSocket *ss)
{
    PRBool isTLS12;
    const PRUint8 *certTypes;
    SECStatus rv;
    PRUint32 length;
    const SECItem *names;
    unsigned int calen;
    unsigned int nnames;
    const SECItem *name;
    unsigned int i;
    int certTypesLength;
    PRUint8 sigAlgs[2 + MAX_SIGNATURE_SCHEMES * 2];
    sslBuffer sigAlgsBuf = SSL_BUFFER(sigAlgs);

    SSL_TRC(3, ("%d: SSL3[%d]: send certificate_request handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    isTLS12 = (PRBool)(ss->version >= SSL_LIBRARY_VERSION_TLS_1_2);

    rv = ssl_GetCertificateRequestCAs(ss, &calen, &names, &nnames);
    if (rv != SECSuccess) {
        return rv;
    }
    certTypes = certificate_types;
    certTypesLength = sizeof certificate_types;

    length = 1 + certTypesLength + 2 + calen;
    if (isTLS12) {
        rv = ssl3_EncodeSigAlgs(ss, ss->version, PR_TRUE /* forCert */,
                                PR_FALSE /* GREASE */, &sigAlgsBuf);
        if (rv != SECSuccess) {
            return rv;
        }
        length += SSL_BUFFER_LEN(&sigAlgsBuf);
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate_request, length);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }
    rv = ssl3_AppendHandshakeVariable(ss, certTypes, certTypesLength, 1);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }
    if (isTLS12) {
        rv = ssl3_AppendHandshake(ss, SSL_BUFFER_BASE(&sigAlgsBuf),
                                  SSL_BUFFER_LEN(&sigAlgsBuf));
        if (rv != SECSuccess) {
            return rv; /* err set by AppendHandshake. */
        }
    }
    rv = ssl3_AppendHandshakeNumber(ss, calen, 2);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }
    for (i = 0, name = names; i < nnames; i++, name++) {
        rv = ssl3_AppendHandshakeVariable(ss, name->data, name->len, 2);
        if (rv != SECSuccess) {
            return rv; /* err set by AppendHandshake. */
        }
    }

    return SECSuccess;
}

static SECStatus
ssl3_SendServerHelloDone(sslSocket *ss)
{
    SECStatus rv;

    SSL_TRC(3, ("%d: SSL3[%d]: send server_hello_done handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_server_hello_done, 0);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }
    rv = ssl3_FlushHandshake(ss, 0);
    if (rv != SECSuccess) {
        return rv; /* error code set by ssl3_FlushHandshake */
    }
    return SECSuccess;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 Certificate Verify message
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleCertificateVerify(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECItem signed_hash = { siBuffer, NULL, 0 };
    SECStatus rv;
    int errCode = SSL_ERROR_RX_MALFORMED_CERT_VERIFY;
    SSL3AlertDescription desc = handshake_failure;
    PRBool isTLS;
    SSLSignatureScheme sigScheme;
    SSL3Hashes hashes;
    const PRUint8 *savedMsg = b;
    const PRUint32 savedLen = length;

    SSL_TRC(3, ("%d: SSL3[%d]: handle certificate_verify handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->ssl3.hs.ws != wait_cert_verify) {
        desc = unexpected_message;
        errCode = SSL_ERROR_RX_UNEXPECTED_CERT_VERIFY;
        goto alert_loser;
    }

    /* TLS 1.3 is handled by tls13_HandleCertificateVerify */
    PORT_Assert(ss->ssl3.prSpec->version <= SSL_LIBRARY_VERSION_TLS_1_2);

    if (ss->ssl3.prSpec->version == SSL_LIBRARY_VERSION_TLS_1_2) {
        PORT_Assert(ss->ssl3.hs.hashType == handshake_hash_record);
        rv = ssl_ConsumeSignatureScheme(ss, &b, &length, &sigScheme);
        if (rv != SECSuccess) {
            if (PORT_GetError() == SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM) {
                errCode = SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM;
            }
            goto loser; /* alert already sent */
        }
        rv = ssl_CheckSignatureSchemeConsistency(
            ss, sigScheme, &ss->sec.peerCert->subjectPublicKeyInfo);
        if (rv != SECSuccess) {
            errCode = PORT_GetError();
            desc = illegal_parameter;
            goto alert_loser;
        }

        rv = ssl3_ComputeHandshakeHash(ss->ssl3.hs.messages.buf,
                                       ss->ssl3.hs.messages.len,
                                       ssl_SignatureSchemeToHashType(sigScheme),
                                       &hashes);
    } else {
        PORT_Assert(ss->ssl3.hs.hashType != handshake_hash_record);
        sigScheme = ssl_sig_none;
        rv = ssl3_ComputeHandshakeHashes(ss, ss->ssl3.prSpec, &hashes, 0);
    }

    if (rv != SECSuccess) {
        errCode = SSL_ERROR_DIGEST_FAILURE;
        desc = decrypt_error;
        goto alert_loser;
    }

    rv = ssl3_ConsumeHandshakeVariable(ss, &signed_hash, 2, &b, &length);
    if (rv != SECSuccess) {
        goto loser; /* malformed. */
    }

    isTLS = (PRBool)(ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0);

    /* XXX verify that the key & kea match */
    rv = ssl3_VerifySignedHashes(ss, sigScheme, &hashes, &signed_hash);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        desc = isTLS ? decrypt_error : handshake_failure;
        goto alert_loser;
    }

    signed_hash.data = NULL;

    if (length != 0) {
        desc = isTLS ? decode_error : illegal_parameter;
        goto alert_loser; /* malformed */
    }

    rv = ssl_HashHandshakeMessage(ss, ssl_hs_certificate_verify,
                                  savedMsg, savedLen);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }

    ss->ssl3.hs.ws = wait_change_cipher;
    return SECSuccess;

alert_loser:
    SSL3_SendAlert(ss, alert_fatal, desc);
loser:
    PORT_SetError(errCode);
    return SECFailure;
}

/* find a slot that is able to generate a PMS and wrap it with RSA.
 * Then generate and return the PMS.
 * If the serverKeySlot parameter is non-null, this function will use
 * that slot to do the job, otherwise it will find a slot.
 *
 * Called from  ssl3_DeriveConnectionKeys()  (above)
 *      ssl3_SendRSAClientKeyExchange()     (above)
 *      ssl3_HandleRSAClientKeyExchange()  (below)
 * Caller must hold the SpecWriteLock, the SSL3HandshakeLock
 */
static PK11SymKey *
ssl3_GenerateRSAPMS(sslSocket *ss, ssl3CipherSpec *spec,
                    PK11SlotInfo *serverKeySlot)
{
    PK11SymKey *pms = NULL;
    PK11SlotInfo *slot = serverKeySlot;
    void *pwArg = ss->pkcs11PinArg;
    SECItem param;
    CK_VERSION version;
    CK_MECHANISM_TYPE mechanism_array[3];

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (slot == NULL) {
        SSLCipherAlgorithm calg;
        /* The specReadLock would suffice here, but we cannot assert on
        ** read locks.  Also, all the callers who call with a non-null
        ** slot already hold the SpecWriteLock.
        */
        PORT_Assert(ss->opt.noLocks || ssl_HaveSpecWriteLock(ss));
        PORT_Assert(ss->ssl3.prSpec->epoch == ss->ssl3.pwSpec->epoch);

        calg = spec->cipherDef->calg;

        /* First get an appropriate slot.  */
        mechanism_array[0] = CKM_SSL3_PRE_MASTER_KEY_GEN;
        mechanism_array[1] = CKM_RSA_PKCS;
        mechanism_array[2] = ssl3_Alg2Mech(calg);

        slot = PK11_GetBestSlotMultiple(mechanism_array, 3, pwArg);
        if (slot == NULL) {
            /* can't find a slot with all three, find a slot with the minimum */
            slot = PK11_GetBestSlotMultiple(mechanism_array, 2, pwArg);
            if (slot == NULL) {
                PORT_SetError(SSL_ERROR_TOKEN_SLOT_NOT_FOUND);
                return pms; /* which is NULL */
            }
        }
    }

    /* Generate the pre-master secret ...  */
    if (IS_DTLS(ss)) {
        SSL3ProtocolVersion temp;

        temp = dtls_TLSVersionToDTLSVersion(ss->clientHelloVersion);
        version.major = MSB(temp);
        version.minor = LSB(temp);
    } else {
        version.major = MSB(ss->clientHelloVersion);
        version.minor = LSB(ss->clientHelloVersion);
    }

    param.data = (unsigned char *)&version;
    param.len = sizeof version;

    pms = PK11_KeyGen(slot, CKM_SSL3_PRE_MASTER_KEY_GEN, &param, 0, pwArg);
    if (!serverKeySlot)
        PK11_FreeSlot(slot);
    if (pms == NULL) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
    }
    return pms;
}

static void
ssl3_CSwapPK11SymKey(PK11SymKey **x, PK11SymKey **y, PRBool c)
{
    uintptr_t mask = (uintptr_t)c;
    unsigned int i;
    for (i = 1; i < sizeof(uintptr_t) * 8; i <<= 1) {
        mask |= mask << i;
    }
    uintptr_t x_ptr = (uintptr_t)*x;
    uintptr_t y_ptr = (uintptr_t)*y;
    uintptr_t tmp = (x_ptr ^ y_ptr) & mask;
    x_ptr = x_ptr ^ tmp;
    y_ptr = y_ptr ^ tmp;
    *x = (PK11SymKey *)x_ptr;
    *y = (PK11SymKey *)y_ptr;
}

/* Note: The Bleichenbacher attack on PKCS#1 necessitates that we NEVER
 * return any indication of failure of the Client Key Exchange message,
 * where that failure is caused by the content of the client's message.
 * This function must not return SECFailure for any reason that is directly
 * or indirectly caused by the content of the client's encrypted PMS.
 * We must not send an alert and also not drop the connection.
 * Instead, we generate a random PMS.  This will cause a failure
 * in the processing the finished message, which is exactly where
 * the failure must occur.
 *
 * Called from ssl3_HandleClientKeyExchange
 */
static SECStatus
ssl3_HandleRSAClientKeyExchange(sslSocket *ss,
                                PRUint8 *b,
                                PRUint32 length,
                                sslKeyPair *serverKeyPair)
{
    SECStatus rv;
    SECItem enc_pms;
    PK11SymKey *pms = NULL;
    PK11SymKey *fauxPms = NULL;
    PK11SlotInfo *slot = NULL;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->ssl3.prSpec->epoch == ss->ssl3.pwSpec->epoch);

    enc_pms.data = b;
    enc_pms.len = length;

    if (ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0) { /* isTLS */
        PRUint32 kLen;
        rv = ssl3_ConsumeHandshakeNumber(ss, &kLen, 2, &enc_pms.data, &enc_pms.len);
        if (rv != SECSuccess) {
            PORT_SetError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
            return SECFailure;
        }
        if ((unsigned)kLen < enc_pms.len) {
            enc_pms.len = kLen;
        }
    }

    /*
     * Get as close to algorithm 2 from RFC 5246; Section 7.4.7.1
     * as we can within the constraints of the PKCS#11 interface.
     *
     * 1. Unconditionally generate a bogus PMS (what RFC 5246
     *    calls R).
     * 2. Attempt the RSA decryption to recover the PMS (what
     *    RFC 5246 calls M).
     * 3. Set PMS = (M == NULL) ? R : M
     * 4. Use ssl3_ComputeMasterSecret(PMS) to attempt to derive
     *    the MS from PMS. This includes performing the version
     *    check and length check.
     * 5. If either the initial RSA decryption failed or
     *    ssl3_ComputeMasterSecret(PMS) failed, then discard
     *    M and set PMS = R. Else, discard R and set PMS = M.
     *
     * We do two derivations here because we can't rely on having
     * a function that only performs the PMS version and length
     * check. The only redundant cost is that this runs the PRF,
     * which isn't necessary here.
     */

    /* Generate the bogus PMS (R) */
    slot = PK11_GetSlotFromPrivateKey(serverKeyPair->privKey);
    if (!slot) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (!PK11_DoesMechanism(slot, CKM_SSL3_MASTER_KEY_DERIVE)) {
        PK11_FreeSlot(slot);
        slot = PK11_GetBestSlot(CKM_SSL3_MASTER_KEY_DERIVE, NULL);
        if (!slot) {
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
    }

    ssl_GetSpecWriteLock(ss);
    fauxPms = ssl3_GenerateRSAPMS(ss, ss->ssl3.prSpec, slot);
    ssl_ReleaseSpecWriteLock(ss);
    PK11_FreeSlot(slot);

    if (fauxPms == NULL) {
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        return SECFailure;
    }

    /*
     * unwrap pms out of the incoming buffer
     * Note: CKM_SSL3_MASTER_KEY_DERIVE is NOT the mechanism used to do
     *  the unwrap.  Rather, it is the mechanism with which the
     *      unwrapped pms will be used.
     */
    pms = PK11_PubUnwrapSymKey(serverKeyPair->privKey, &enc_pms,
                               CKM_SSL3_MASTER_KEY_DERIVE, CKA_DERIVE, 0);
    /* Temporarily use the PMS if unwrapping the real PMS fails. */
    ssl3_CSwapPK11SymKey(&pms, &fauxPms, pms == NULL);

    /* Attempt to derive the MS from the PMS. This is the only way to
     * check the version field in the RSA PMS. If this fails, we
     * then use the faux PMS in place of the PMS. Note that this
     * operation should never fail if we are using the faux PMS
     * since it is correctly formatted. */
    rv = ssl3_ComputeMasterSecret(ss, pms, NULL);

    /* If we succeeded, then select the true PMS, else select the FPMS. */
    ssl3_CSwapPK11SymKey(&pms, &fauxPms, (rv != SECSuccess) & (fauxPms != NULL));

    /* This step will derive the MS from the PMS, among other things. */
    rv = ssl3_InitPendingCipherSpecs(ss, pms, PR_TRUE);

    /* Clear both PMS. */
    PK11_FreeSymKey(pms);
    PK11_FreeSymKey(fauxPms);

    if (rv != SECSuccess) {
        (void)SSL3_SendAlert(ss, alert_fatal, handshake_failure);
        return SECFailure; /* error code set by ssl3_InitPendingCipherSpec */
    }

    return SECSuccess;
}

static SECStatus
ssl3_HandleDHClientKeyExchange(sslSocket *ss,
                               PRUint8 *b,
                               PRUint32 length,
                               sslKeyPair *serverKeyPair)
{
    PK11SymKey *pms;
    SECStatus rv;
    SECKEYPublicKey clntPubKey;
    CK_MECHANISM_TYPE target;
    PRBool isTLS;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    clntPubKey.keyType = dhKey;
    clntPubKey.u.dh.prime.len = serverKeyPair->pubKey->u.dh.prime.len;
    clntPubKey.u.dh.prime.data = serverKeyPair->pubKey->u.dh.prime.data;
    clntPubKey.u.dh.base.len = serverKeyPair->pubKey->u.dh.base.len;
    clntPubKey.u.dh.base.data = serverKeyPair->pubKey->u.dh.base.data;

    rv = ssl3_ConsumeHandshakeVariable(ss, &clntPubKey.u.dh.publicValue,
                                       2, &b, &length);
    if (rv != SECSuccess) {
        return SECFailure;
    }

    if (!ssl_IsValidDHEShare(&serverKeyPair->pubKey->u.dh.prime,
                             &clntPubKey.u.dh.publicValue)) {
        PORT_SetError(SSL_ERROR_RX_MALFORMED_DHE_KEY_SHARE);
        return SECFailure;
    }

    isTLS = (PRBool)(ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0);

    if (isTLS)
        target = CKM_TLS_MASTER_KEY_DERIVE_DH;
    else
        target = CKM_SSL3_MASTER_KEY_DERIVE_DH;

    /* Determine the PMS */
    pms = PK11_PubDerive(serverKeyPair->privKey, &clntPubKey, PR_FALSE, NULL, NULL,
                         CKM_DH_PKCS_DERIVE, target, CKA_DERIVE, 0, NULL);
    if (pms == NULL) {
        ssl_FreeEphemeralKeyPairs(ss);
        ssl_MapLowLevelError(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE);
        return SECFailure;
    }

    rv = ssl3_InitPendingCipherSpecs(ss, pms, PR_TRUE);
    PK11_FreeSymKey(pms);
    ssl_FreeEphemeralKeyPairs(ss);
    return rv;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 ClientKeyExchange message from the remote client
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleClientKeyExchange(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    sslKeyPair *serverKeyPair = NULL;
    SECStatus rv;
    const ssl3KEADef *kea_def;

    SSL_TRC(3, ("%d: SSL3[%d]: handle client_key_exchange handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (ss->ssl3.hs.ws != wait_client_key) {
        SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CLIENT_KEY_EXCH);
        return SECFailure;
    }

    kea_def = ss->ssl3.hs.kea_def;

    if (kea_def->ephemeral) {
        sslEphemeralKeyPair *keyPair;
        /* There should be exactly one pair. */
        PORT_Assert(!PR_CLIST_IS_EMPTY(&ss->ephemeralKeyPairs));
        PORT_Assert(PR_PREV_LINK(&ss->ephemeralKeyPairs) ==
                    PR_NEXT_LINK(&ss->ephemeralKeyPairs));
        keyPair = (sslEphemeralKeyPair *)PR_NEXT_LINK(&ss->ephemeralKeyPairs);
        serverKeyPair = keyPair->keys;
        ss->sec.keaKeyBits =
            SECKEY_PublicKeyStrengthInBits(serverKeyPair->pubKey);
    } else {
        serverKeyPair = ss->sec.serverCert->serverKeyPair;
        ss->sec.keaKeyBits = ss->sec.serverCert->serverKeyBits;
    }

    if (!serverKeyPair) {
        SSL3_SendAlert(ss, alert_fatal, handshake_failure);
        PORT_SetError(SSL_ERROR_NO_SERVER_KEY_FOR_ALG);
        return SECFailure;
    }
    PORT_Assert(serverKeyPair->pubKey);
    PORT_Assert(serverKeyPair->privKey);

    ss->sec.keaType = kea_def->exchKeyType;

    switch (kea_def->exchKeyType) {
        case ssl_kea_rsa:
            rv = ssl3_HandleRSAClientKeyExchange(ss, b, length, serverKeyPair);
            break;

        case ssl_kea_dh:
            rv = ssl3_HandleDHClientKeyExchange(ss, b, length, serverKeyPair);
            break;

        case ssl_kea_ecdh:
            rv = ssl3_HandleECDHClientKeyExchange(ss, b, length, serverKeyPair);
            break;

        default:
            (void)ssl3_HandshakeFailure(ss);
            PORT_SetError(SEC_ERROR_UNSUPPORTED_KEYALG);
            return SECFailure;
    }
    ssl_FreeEphemeralKeyPairs(ss);
    if (rv == SECSuccess) {
        ss->ssl3.hs.ws = ss->sec.peerCert ? wait_cert_verify : wait_change_cipher;
    } else {
        /* PORT_SetError has been called by all the Handle*ClientKeyExchange
         * functions above.  However, not all error paths result in an alert, so
         * this ensures that the server knows about the error.  Note that if an
         * alert was already sent, SSL3_SendAlert() is a noop. */
        PRErrorCode errCode = PORT_GetError();
        (void)SSL3_SendAlert(ss, alert_fatal, handshake_failure);
        PORT_SetError(errCode);
    }
    return rv;
}

/* This is TLS's equivalent of sending a no_certificate alert. */
SECStatus
ssl3_SendEmptyCertificate(sslSocket *ss)
{
    SECStatus rv;
    unsigned int len = 0;
    PRBool isTLS13 = PR_FALSE;
    const SECItem *context;

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        PORT_Assert(ss->ssl3.hs.clientCertRequested);
        context = &ss->xtnData.certReqContext;
        len = context->len + 1;
        isTLS13 = PR_TRUE;
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate, len + 3);
    if (rv != SECSuccess) {
        return rv;
    }

    if (isTLS13) {
        rv = ssl3_AppendHandshakeVariable(ss, context->data, context->len, 1);
        if (rv != SECSuccess) {
            return rv;
        }
    }

    return ssl3_AppendHandshakeNumber(ss, 0, 3);
}

/*
 * NewSessionTicket
 * Called from ssl3_HandleFinished
 */
static SECStatus
ssl3_SendNewSessionTicket(sslSocket *ss)
{
    SECItem ticket = { 0, NULL, 0 };
    SECStatus rv;
    NewSessionTicket nticket = { 0 };

    rv = ssl3_EncodeSessionTicket(ss, &nticket, NULL, 0,
                                  ss->ssl3.pwSpec->masterSecret, &ticket);
    if (rv != SECSuccess)
        goto loser;

    /* Serialize the handshake message. Length =
     * lifetime (4) + ticket length (2) + ticket. */
    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_new_session_ticket,
                                    4 + 2 + ticket.len);
    if (rv != SECSuccess)
        goto loser;

    /* This is a fixed value. */
    rv = ssl3_AppendHandshakeNumber(ss, ssl_ticket_lifetime, 4);
    if (rv != SECSuccess)
        goto loser;

    /* Encode the ticket. */
    rv = ssl3_AppendHandshakeVariable(ss, ticket.data, ticket.len, 2);
    if (rv != SECSuccess)
        goto loser;

    rv = SECSuccess;

loser:
    if (ticket.data) {
        SECITEM_FreeItem(&ticket, PR_FALSE);
    }
    return rv;
}

static SECStatus
ssl3_HandleNewSessionTicket(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;
    SECItem ticketData;
    PRUint32 temp;

    SSL_TRC(3, ("%d: SSL3[%d]: handle session_ticket handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    PORT_Assert(!ss->ssl3.hs.newSessionTicket.ticket.data);
    PORT_Assert(!ss->ssl3.hs.receivedNewSessionTicket);

    if (ss->ssl3.hs.ws != wait_new_session_ticket) {
        SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_NEW_SESSION_TICKET);
        return SECFailure;
    }

    /* RFC5077 Section 3.3: "The client MUST NOT treat the ticket as valid
     * until it has verified the server's Finished message." See the comment in
     * ssl3_FinishHandshake for more details.
     */
    ss->ssl3.hs.newSessionTicket.received_timestamp = ssl_Time(ss);
    if (length < 4) {
        (void)SSL3_SendAlert(ss, alert_fatal, decode_error);
        PORT_SetError(SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET);
        return SECFailure;
    }

    rv = ssl3_ConsumeHandshakeNumber(ss, &temp, 4, &b, &length);
    if (rv != SECSuccess) {
        PORT_SetError(SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET);
        return SECFailure;
    }
    ss->ssl3.hs.newSessionTicket.ticket_lifetime_hint = temp;

    rv = ssl3_ConsumeHandshakeVariable(ss, &ticketData, 2, &b, &length);
    if (rv != SECSuccess || length != 0) {
        (void)SSL3_SendAlert(ss, alert_fatal, decode_error);
        PORT_SetError(SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET);
        return SECFailure; /* malformed */
    }
    /* If the server sent a zero-length ticket, ignore it and keep the
     * existing ticket. */
    if (ticketData.len != 0) {
        rv = SECITEM_CopyItem(NULL, &ss->ssl3.hs.newSessionTicket.ticket,
                              &ticketData);
        if (rv != SECSuccess) {
            return rv;
        }
        ss->ssl3.hs.receivedNewSessionTicket = PR_TRUE;
    }

    ss->ssl3.hs.ws = wait_change_cipher;
    return SECSuccess;
}

#ifdef NISCC_TEST
static PRInt32 connNum = 0;

static SECStatus
get_fake_cert(SECItem *pCertItem, int *pIndex)
{
    PRFileDesc *cf;
    char *testdir;
    char *startat;
    char *stopat;
    const char *extension;
    int fileNum;
    PRInt32 numBytes = 0;
    PRStatus prStatus;
    PRFileInfo info;
    char cfn[100];

    pCertItem->data = 0;
    if ((testdir = PR_GetEnvSecure("NISCC_TEST")) == NULL) {
        return SECSuccess;
    }
    *pIndex = (NULL != strstr(testdir, "root"));
    extension = (strstr(testdir, "simple") ? "" : ".der");
    fileNum = PR_ATOMIC_INCREMENT(&connNum) - 1;
    if ((startat = PR_GetEnvSecure("START_AT")) != NULL) {
        fileNum += atoi(startat);
    }
    if ((stopat = PR_GetEnvSecure("STOP_AT")) != NULL &&
        fileNum >= atoi(stopat)) {
        *pIndex = -1;
        return SECSuccess;
    }
    snprintf(cfn, sizeof(cfn), "%s/%08d%s", testdir, fileNum, extension);
    cf = PR_Open(cfn, PR_RDONLY, 0);
    if (!cf) {
        goto loser;
    }
    prStatus = PR_GetOpenFileInfo(cf, &info);
    if (prStatus != PR_SUCCESS) {
        PR_Close(cf);
        goto loser;
    }
    pCertItem = SECITEM_AllocItem(NULL, pCertItem, info.size);
    if (pCertItem) {
        numBytes = PR_Read(cf, pCertItem->data, info.size);
    }
    PR_Close(cf);
    if (numBytes != info.size) {
        SECITEM_FreeItem(pCertItem, PR_FALSE);
        PORT_SetError(SEC_ERROR_IO);
        goto loser;
    }
    fprintf(stderr, "using %s\n", cfn);
    return SECSuccess;

loser:
    fprintf(stderr, "failed to use %s\n", cfn);
    *pIndex = -1;
    return SECFailure;
}
#endif

/*
 * Used by both client and server.
 * Called from HandleServerHelloDone and from SendServerHelloSequence.
 */
static SECStatus
ssl3_SendCertificate(sslSocket *ss)
{
    SECStatus rv;
    CERTCertificateList *certChain;
    int certChainLen = 0;
    int i;
#ifdef NISCC_TEST
    SECItem fakeCert;
    int ndex = -1;
#endif
    PRBool isTLS13 = ss->version >= SSL_LIBRARY_VERSION_TLS_1_3;
    SECItem context = { siBuffer, NULL, 0 };
    unsigned int contextLen = 0;

    SSL_TRC(3, ("%d: SSL3[%d]: send certificate handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PR_ASSERT(!ss->ssl3.hs.clientCertificatePending);

    if (ss->sec.localCert)
        CERT_DestroyCertificate(ss->sec.localCert);
    if (ss->sec.isServer) {
        /* A server certificate is selected in ssl3_HandleClientHello. */
        PORT_Assert(ss->sec.serverCert);

        certChain = ss->sec.serverCert->serverCertChain;
        ss->sec.localCert = CERT_DupCertificate(ss->sec.serverCert->serverCert);
    } else {
        certChain = ss->ssl3.clientCertChain;
        ss->sec.localCert = CERT_DupCertificate(ss->ssl3.clientCertificate);
    }

#ifdef NISCC_TEST
    rv = get_fake_cert(&fakeCert, &ndex);
#endif

    if (isTLS13) {
        contextLen = 1; /* Size of the context length */
        if (!ss->sec.isServer) {
            PORT_Assert(ss->ssl3.hs.clientCertRequested);
            context = ss->xtnData.certReqContext;
            contextLen += context.len;
        }
    }
    if (certChain) {
        for (i = 0; i < certChain->len; i++) {
#ifdef NISCC_TEST
            if (fakeCert.len > 0 && i == ndex) {
                certChainLen += fakeCert.len + 3;
            } else {
                certChainLen += certChain->certs[i].len + 3;
            }
#else
            certChainLen += certChain->certs[i].len + 3;
#endif
        }
    }

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate,
                                    contextLen + certChainLen + 3);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }

    if (isTLS13) {
        rv = ssl3_AppendHandshakeVariable(ss, context.data,
                                          context.len, 1);
        if (rv != SECSuccess) {
            return rv; /* err set by AppendHandshake. */
        }
    }

    rv = ssl3_AppendHandshakeNumber(ss, certChainLen, 3);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }
    if (certChain) {
        for (i = 0; i < certChain->len; i++) {
#ifdef NISCC_TEST
            if (fakeCert.len > 0 && i == ndex) {
                rv = ssl3_AppendHandshakeVariable(ss, fakeCert.data,
                                                  fakeCert.len, 3);
                SECITEM_FreeItem(&fakeCert, PR_FALSE);
            } else {
                rv = ssl3_AppendHandshakeVariable(ss, certChain->certs[i].data,
                                                  certChain->certs[i].len, 3);
            }
#else
            rv = ssl3_AppendHandshakeVariable(ss, certChain->certs[i].data,
                                              certChain->certs[i].len, 3);
#endif
            if (rv != SECSuccess) {
                return rv; /* err set by AppendHandshake. */
            }
        }
    }

    return SECSuccess;
}

/*
 * Used by server only.
 * single-stapling, send only a single cert status
 */
SECStatus
ssl3_SendCertificateStatus(sslSocket *ss)
{
    SECStatus rv;
    int len = 0;
    SECItemArray *statusToSend = NULL;
    const sslServerCert *serverCert;

    SSL_TRC(3, ("%d: SSL3[%d]: send certificate status handshake",
                SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->sec.isServer);

    if (!ssl3_ExtensionNegotiated(ss, ssl_cert_status_xtn))
        return SECSuccess;

    /* Use certStatus based on the cert being used. */
    serverCert = ss->sec.serverCert;
    if (serverCert->certStatusArray && serverCert->certStatusArray->len) {
        statusToSend = serverCert->certStatusArray;
    }
    if (!statusToSend)
        return SECSuccess;

    /* Use the array's first item only (single stapling) */
    len = 1 + statusToSend->items[0].len + 3;

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_certificate_status, len);
    if (rv != SECSuccess) {
        return rv; /* err set by AppendHandshake. */
    }
    rv = ssl3_AppendHandshakeNumber(ss, 1 /*ocsp*/, 1);
    if (rv != SECSuccess)
        return rv; /* err set by AppendHandshake. */

    rv = ssl3_AppendHandshakeVariable(ss,
                                      statusToSend->items[0].data,
                                      statusToSend->items[0].len,
                                      3);
    if (rv != SECSuccess)
        return rv; /* err set by AppendHandshake. */

    return SECSuccess;
}

/* This is used to delete the CA certificates in the peer certificate chain
 * from the cert database after they've been validated.
 */
void
ssl3_CleanupPeerCerts(sslSocket *ss)
{
    PLArenaPool *arena = ss->ssl3.peerCertArena;

    if (arena)
        PORT_FreeArena(arena, PR_FALSE);
    ss->ssl3.peerCertArena = NULL;
    ss->ssl3.peerCertChain = NULL;

    if (ss->sec.peerCert != NULL) {
        if (ss->sec.peerKey) {
            SECKEY_DestroyPublicKey(ss->sec.peerKey);
            ss->sec.peerKey = NULL;
        }
        CERT_DestroyCertificate(ss->sec.peerCert);
        ss->sec.peerCert = NULL;
    }
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 CertificateStatus message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleCertificateStatus(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv;

    if (ss->ssl3.hs.ws != wait_certificate_status) {
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CERT_STATUS);
        return SECFailure;
    }

    rv = ssl_ReadCertificateStatus(ss, b, length);
    if (rv != SECSuccess) {
        return SECFailure; /* code already set */
    }

    return ssl3_AuthCertificate(ss);
}

SECStatus
ssl_ReadCertificateStatus(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    PRUint32 status, len;
    SECStatus rv;

    PORT_Assert(!ss->sec.isServer);

    /* Consume the CertificateStatusType enum */
    rv = ssl3_ConsumeHandshakeNumber(ss, &status, 1, &b, &length);
    if (rv != SECSuccess || status != 1 /* ocsp */) {
        return ssl3_DecodeError(ss);
    }

    rv = ssl3_ConsumeHandshakeNumber(ss, &len, 3, &b, &length);
    if (rv != SECSuccess || len != length) {
        return ssl3_DecodeError(ss);
    }

#define MAX_CERTSTATUS_LEN 0x1ffff /* 128k - 1 */
    if (length > MAX_CERTSTATUS_LEN) {
        ssl3_DecodeError(ss); /* sets error code */
        return SECFailure;
    }
#undef MAX_CERTSTATUS_LEN

    /* Array size 1, because we currently implement single-stapling only */
    SECITEM_AllocArray(NULL, &ss->sec.ci.sid->peerCertStatus, 1);
    if (!ss->sec.ci.sid->peerCertStatus.items)
        return SECFailure; /* code already set */

    ss->sec.ci.sid->peerCertStatus.items[0].data = PORT_Alloc(length);

    if (!ss->sec.ci.sid->peerCertStatus.items[0].data) {
        SECITEM_FreeArray(&ss->sec.ci.sid->peerCertStatus, PR_FALSE);
        return SECFailure; /* code already set */
    }

    PORT_Memcpy(ss->sec.ci.sid->peerCertStatus.items[0].data, b, length);
    ss->sec.ci.sid->peerCertStatus.items[0].len = length;
    ss->sec.ci.sid->peerCertStatus.items[0].type = siBuffer;
    return SECSuccess;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 Certificate message.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleCertificate(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SSL_TRC(3, ("%d: SSL3[%d]: handle certificate handshake",
                SSL_GETPID(), ss->fd));
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if ((ss->sec.isServer && ss->ssl3.hs.ws != wait_client_cert) ||
        (!ss->sec.isServer && ss->ssl3.hs.ws != wait_server_cert)) {
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CERTIFICATE);
        return SECFailure;
    }

    if (ss->sec.isServer) {
        dtls_ReceivedFirstMessageInFlight(ss);
    }

    return ssl3_CompleteHandleCertificate(ss, b, length);
}

/* Called from ssl3_HandleCertificate
 */
SECStatus
ssl3_CompleteHandleCertificate(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    ssl3CertNode *c;
    ssl3CertNode *lastCert = NULL;
    PRUint32 remaining = 0;
    PRUint32 size;
    SECStatus rv;
    PRBool isServer = ss->sec.isServer;
    PRBool isTLS;
    SSL3AlertDescription desc;
    int errCode = SSL_ERROR_RX_MALFORMED_CERTIFICATE;
    SECItem certItem;

    ssl3_CleanupPeerCerts(ss);
    isTLS = (PRBool)(ss->ssl3.prSpec->version > SSL_LIBRARY_VERSION_3_0);

    /* It is reported that some TLS client sends a Certificate message
    ** with a zero-length message body.  We'll treat that case like a
    ** normal no_certificates message to maximize interoperability.
    */
    if (length) {
        rv = ssl3_ConsumeHandshakeNumber(ss, &remaining, 3, &b, &length);
        if (rv != SECSuccess)
            goto loser; /* fatal alert already sent by ConsumeHandshake. */
        if (remaining > length)
            goto decode_loser;
    }

    if (!remaining) {
        if (!(isTLS && isServer)) {
            desc = bad_certificate;
            goto alert_loser;
        }
        /* This is TLS's version of a no_certificate alert. */
        /* I'm a server. I've requested a client cert. He hasn't got one. */
        rv = ssl3_HandleNoCertificate(ss);
        if (rv != SECSuccess) {
            errCode = PORT_GetError();
            goto loser;
        }

        if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
            ss->ssl3.hs.ws = wait_client_key;
        } else {
            TLS13_SET_HS_STATE(ss, wait_finished);
        }
        return SECSuccess;
    }

    ss->ssl3.peerCertArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (ss->ssl3.peerCertArena == NULL) {
        goto loser; /* don't send alerts on memory errors */
    }

    /* First get the peer cert. */
    if (remaining < 3)
        goto decode_loser;

    remaining -= 3;
    rv = ssl3_ConsumeHandshakeNumber(ss, &size, 3, &b, &length);
    if (rv != SECSuccess)
        goto loser; /* fatal alert already sent by ConsumeHandshake. */
    if (size == 0 || remaining < size)
        goto decode_loser;

    certItem.data = b;
    certItem.len = size;
    b += size;
    length -= size;
    remaining -= size;

    ss->sec.peerCert = CERT_NewTempCertificate(ss->dbHandle, &certItem, NULL,
                                               PR_FALSE, PR_TRUE);
    if (ss->sec.peerCert == NULL) {
        /* We should report an alert if the cert was bad, but not if the
         * problem was just some local problem, like memory error.
         */
        goto ambiguous_err;
    }

    /* Now get all of the CA certs. */
    while (remaining > 0) {
        if (remaining < 3)
            goto decode_loser;

        remaining -= 3;
        rv = ssl3_ConsumeHandshakeNumber(ss, &size, 3, &b, &length);
        if (rv != SECSuccess)
            goto loser; /* fatal alert already sent by ConsumeHandshake. */
        if (size == 0 || remaining < size)
            goto decode_loser;

        certItem.data = b;
        certItem.len = size;
        b += size;
        length -= size;
        remaining -= size;

        c = PORT_ArenaNew(ss->ssl3.peerCertArena, ssl3CertNode);
        if (c == NULL) {
            goto loser; /* don't send alerts on memory errors */
        }

        c->derCert = SECITEM_ArenaDupItem(ss->ssl3.peerCertArena,
                                          &certItem);
        if (c->derCert == NULL) {
            goto loser;
        }

        c->next = NULL;
        if (lastCert) {
            lastCert->next = c;
        } else {
            ss->ssl3.peerCertChain = c;
        }
        lastCert = c;
    }

    SECKEY_UpdateCertPQG(ss->sec.peerCert);

    if (!isServer &&
        ss->version < SSL_LIBRARY_VERSION_TLS_1_3 &&
        ssl3_ExtensionNegotiated(ss, ssl_cert_status_xtn)) {
        ss->ssl3.hs.ws = wait_certificate_status;
        rv = SECSuccess;
    } else {
        rv = ssl3_AuthCertificate(ss); /* sets ss->ssl3.hs.ws */
    }

    return rv;

ambiguous_err:
    errCode = PORT_GetError();
    switch (errCode) {
        case PR_OUT_OF_MEMORY_ERROR:
        case SEC_ERROR_BAD_DATABASE:
        case SEC_ERROR_NO_MEMORY:
            if (isTLS) {
                desc = internal_error;
                goto alert_loser;
            }
            goto loser;
    }
    ssl3_SendAlertForCertError(ss, errCode);
    goto loser;

decode_loser:
    desc = isTLS ? decode_error : bad_certificate;

alert_loser:
    (void)SSL3_SendAlert(ss, alert_fatal, desc);

loser:
    (void)ssl_MapLowLevelError(errCode);
    return SECFailure;
}

SECStatus
ssl_SetAuthKeyBits(sslSocket *ss, const SECKEYPublicKey *pubKey)
{
    SECStatus rv;
    PRUint32 minKey = 0;
    PRInt32 optval;
    PRBool usePolicyLength = PR_TRUE;

    rv = NSS_OptionGet(NSS_KEY_SIZE_POLICY_FLAGS, &optval);
    if (rv == SECSuccess) {
        usePolicyLength = (PRBool)((optval & NSS_KEY_SIZE_POLICY_SSL_FLAG) == NSS_KEY_SIZE_POLICY_SSL_FLAG);
    }

    ss->sec.authKeyBits = SECKEY_PublicKeyStrengthInBits(pubKey);
    switch (SECKEY_GetPublicKeyType(pubKey)) {
        case rsaKey:
        case rsaPssKey:
        case rsaOaepKey:
            rv = usePolicyLength ? NSS_OptionGet(NSS_RSA_MIN_KEY_SIZE, &optval)
                                 : SECFailure;
            if (rv == SECSuccess && optval > 0) {
                minKey = (PRUint32)optval;
            } else {
                minKey = SSL_RSA_MIN_MODULUS_BITS;
            }
            break;

        case dsaKey:
            rv = usePolicyLength ? NSS_OptionGet(NSS_DSA_MIN_KEY_SIZE, &optval)
                                 : SECFailure;
            if (rv == SECSuccess && optval > 0) {
                minKey = (PRUint32)optval;
            } else {
                minKey = SSL_DSA_MIN_P_BITS;
            }
            break;

        case dhKey:
            rv = usePolicyLength ? NSS_OptionGet(NSS_DH_MIN_KEY_SIZE, &optval)
                                 : SECFailure;
            if (rv == SECSuccess && optval > 0) {
                minKey = (PRUint32)optval;
            } else {
                minKey = SSL_DH_MIN_P_BITS;
            }
            break;

        case ecKey:
            rv = usePolicyLength ? NSS_OptionGet(NSS_ECC_MIN_KEY_SIZE, &optval)
                                 : SECFailure;
            if (rv == SECSuccess && optval > 0) {
                minKey = (PRUint32)optval;
            } else {
                /* Don't check EC strength here on the understanding that we
                 * only support curves we like. */
                minKey = ss->sec.authKeyBits;
            }
            break;

        default:
            FATAL_ERROR(ss, SEC_ERROR_LIBRARY_FAILURE, internal_error);
            return SECFailure;
    }

    /* Too small: not good enough. Send a fatal alert. */
    if (ss->sec.authKeyBits < minKey) {
        FATAL_ERROR(ss, SSL_ERROR_WEAK_SERVER_CERT_KEY,
                    ss->version >= SSL_LIBRARY_VERSION_TLS_1_0
                        ? insufficient_security
                        : illegal_parameter);
        return SECFailure;
    }

    /* PreliminaryChannelInfo.authKeyBits, scheme, and peerDelegCred are now valid. */
    ss->ssl3.hs.preliminaryInfo |= ssl_preinfo_peer_auth;

    return SECSuccess;
}

SECStatus
ssl3_HandleServerSpki(sslSocket *ss)
{
    PORT_Assert(!ss->sec.isServer);
    SECKEYPublicKey *pubKey;

    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
        tls13_IsVerifyingWithDelegatedCredential(ss)) {
        sslDelegatedCredential *dc = ss->xtnData.peerDelegCred;
        pubKey = SECKEY_ExtractPublicKey(dc->spki);
        if (!pubKey) {
            PORT_SetError(SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE);
            return SECFailure;
        }

        /* Because we have only a single authType (ssl_auth_tls13_any)
         * for TLS 1.3 at this point, set the scheme so that the
         * callback can interpret |authKeyBits| correctly.
         */
        ss->sec.signatureScheme = dc->expectedCertVerifyAlg;
    } else {
        pubKey = CERT_ExtractPublicKey(ss->sec.peerCert);
        if (!pubKey) {
            PORT_SetError(SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE);
            return SECFailure;
        }
    }

    SECStatus rv = ssl_SetAuthKeyBits(ss, pubKey);
    SECKEY_DestroyPublicKey(pubKey);
    if (rv != SECSuccess) {
        return rv; /* Alert sent and code set. */
    }

    return SECSuccess;
}

SECStatus
ssl3_AuthCertificate(sslSocket *ss)
{
    SECStatus rv;
    PRBool isServer = ss->sec.isServer;
    int errCode;

    ss->ssl3.hs.authCertificatePending = PR_FALSE;

    PORT_Assert((ss->ssl3.hs.preliminaryInfo & ssl_preinfo_all) ==
                ssl_preinfo_all);

    if (!ss->sec.isServer) {
        /* Set the |spki| used to verify the handshake. When verifying with a
         * delegated credential (DC), this corresponds to the DC public key;
         * otherwise it correspond to the public key of the peer's end-entity
         * certificate. */
        rv = ssl3_HandleServerSpki(ss);
        if (rv != SECSuccess) {
            /* Alert sent and code set (if not SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE).
             * In either case, we're done here. */
            errCode = PORT_GetError();
            goto loser;
        }

        if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
            ss->sec.authType = ss->ssl3.hs.kea_def->authKeyType;
            ss->sec.keaType = ss->ssl3.hs.kea_def->exchKeyType;
        }
    }

    /*
     * Ask caller-supplied callback function to validate cert chain.
     */
    rv = (SECStatus)(*ss->authCertificate)(ss->authCertificateArg, ss->fd,
                                           PR_TRUE, isServer);
    if (rv != SECSuccess) {
        errCode = PORT_GetError();
        if (errCode == 0) {
            errCode = SSL_ERROR_BAD_CERTIFICATE;
        }
        if (rv != SECWouldBlock) {
            if (ss->handleBadCert) {
                rv = (*ss->handleBadCert)(ss->badCertArg, ss->fd);
            }
        }

        if (rv == SECWouldBlock) {
            if (ss->sec.isServer) {
                errCode = SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_SERVERS;
                goto loser;
            }

            ss->ssl3.hs.authCertificatePending = PR_TRUE;
            rv = SECSuccess;
        }

        if (rv != SECSuccess) {
            ssl3_SendAlertForCertError(ss, errCode);
            goto loser;
        }
    }

    if (ss->sec.ci.sid->peerCert) {
        CERT_DestroyCertificate(ss->sec.ci.sid->peerCert);
    }
    ss->sec.ci.sid->peerCert = CERT_DupCertificate(ss->sec.peerCert);

    if (!ss->sec.isServer) {
        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
            TLS13_SET_HS_STATE(ss, wait_cert_verify);
        } else {
            /* Ephemeral suites require ServerKeyExchange. */
            if (ss->ssl3.hs.kea_def->ephemeral) {
                /* require server_key_exchange */
                ss->ssl3.hs.ws = wait_server_key;
            } else {
                /* disallow server_key_exchange */
                ss->ssl3.hs.ws = wait_cert_request;
                /* This is static RSA key exchange so set the key exchange
                 * details to compensate for that. */
                ss->sec.keaKeyBits = ss->sec.authKeyBits;
                ss->sec.signatureScheme = ssl_sig_none;
                ss->sec.keaGroup = NULL;
            }
        }
    } else {
        /* Server */
        if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
            ss->ssl3.hs.ws = wait_client_key;
        } else {
            TLS13_SET_HS_STATE(ss, wait_cert_verify);
        }
    }

    PORT_Assert(rv == SECSuccess);
    if (rv != SECSuccess) {
        errCode = SEC_ERROR_LIBRARY_FAILURE;
        goto loser;
    }

    return SECSuccess;

loser:
    (void)ssl_MapLowLevelError(errCode);
    return SECFailure;
}

static SECStatus ssl3_FinishHandshake(sslSocket *ss);

static SECStatus
ssl3_AlwaysFail(sslSocket *ss)
{
    /* The caller should have cleared the callback. */
    ss->ssl3.hs.restartTarget = ssl3_AlwaysFail;
    PORT_SetError(PR_INVALID_STATE_ERROR);
    return SECFailure;
}

/* Caller must hold 1stHandshakeLock.
 */
SECStatus
ssl3_AuthCertificateComplete(sslSocket *ss, PRErrorCode error)
{
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_Have1stHandshakeLock(ss));

    if (ss->sec.isServer) {
        PORT_SetError(SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_SERVERS);
        return SECFailure;
    }

    ssl_GetRecvBufLock(ss);
    ssl_GetSSL3HandshakeLock(ss);

    if (!ss->ssl3.hs.authCertificatePending) {
        PORT_SetError(PR_INVALID_STATE_ERROR);
        rv = SECFailure;
        goto done;
    }

    ss->ssl3.hs.authCertificatePending = PR_FALSE;

    if (error != 0) {
        ss->ssl3.hs.restartTarget = ssl3_AlwaysFail;
        ssl3_SendAlertForCertError(ss, error);
        rv = SECSuccess;
    } else if (ss->ssl3.hs.restartTarget != NULL) {
        sslRestartTarget target = ss->ssl3.hs.restartTarget;
        ss->ssl3.hs.restartTarget = NULL;

        if (target == ssl3_FinishHandshake) {
            SSL_TRC(3, ("%d: SSL3[%p]: certificate authentication lost the race"
                        " with peer's finished message",
                        SSL_GETPID(), ss->fd));
        }

        rv = target(ss);
    } else {
        SSL_TRC(3, ("%d: SSL3[%p]: certificate authentication won the race with"
                    " peer's finished message",
                    SSL_GETPID(), ss->fd));

        PORT_Assert(!ss->ssl3.hs.isResuming);
        PORT_Assert(ss->ssl3.hs.ws != idle_handshake);

        if (ss->opt.enableFalseStart &&
            !ss->firstHsDone &&
            !ss->ssl3.hs.isResuming &&
            ssl3_WaitingForServerSecondRound(ss)) {
            /* ssl3_SendClientSecondRound deferred the false start check because
             * certificate authentication was pending, so we do it now if we still
             * haven't received all of the server's second round yet.
             */
            rv = ssl3_CheckFalseStart(ss);
        } else {
            rv = SECSuccess;
        }
    }

done:
    ssl_ReleaseSSL3HandshakeLock(ss);
    ssl_ReleaseRecvBufLock(ss);

    return rv;
}

static SECStatus
ssl3_ComputeTLSFinished(sslSocket *ss, ssl3CipherSpec *spec,
                        PRBool isServer,
                        const SSL3Hashes *hashes,
                        TLSFinished *tlsFinished)
{
    SECStatus rv;
    CK_TLS_MAC_PARAMS tls_mac_params;
    SECItem param = { siBuffer, NULL, 0 };
    PK11Context *prf_context;
    unsigned int retLen;

    PORT_Assert(spec->masterSecret);
    if (!spec->masterSecret) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (spec->version < SSL_LIBRARY_VERSION_TLS_1_2) {
        tls_mac_params.prfHashMechanism = CKM_TLS_PRF;
    } else {
        tls_mac_params.prfHashMechanism = ssl3_GetPrfHashMechanism(ss);
    }
    tls_mac_params.ulMacLength = 12;
    tls_mac_params.ulServerOrClient = isServer ? 1 : 2;
    param.data = (unsigned char *)&tls_mac_params;
    param.len = sizeof(tls_mac_params);
    prf_context = PK11_CreateContextBySymKey(CKM_TLS_MAC, CKA_SIGN,
                                             spec->masterSecret, &param);
    if (!prf_context)
        return SECFailure;

    rv = PK11_DigestBegin(prf_context);
    rv |= PK11_DigestOp(prf_context, hashes->u.raw, hashes->len);
    rv |= PK11_DigestFinal(prf_context, tlsFinished->verify_data, &retLen,
                           sizeof tlsFinished->verify_data);
    PORT_Assert(rv != SECSuccess || retLen == sizeof tlsFinished->verify_data);

    PK11_DestroyContext(prf_context, PR_TRUE);

    return rv;
}

/* The calling function must acquire and release the appropriate
 * lock (e.g., ssl_GetSpecReadLock / ssl_ReleaseSpecReadLock for
 * ss->ssl3.crSpec).
 */
SECStatus
ssl3_TLSPRFWithMasterSecret(sslSocket *ss, ssl3CipherSpec *spec,
                            const char *label, unsigned int labelLen,
                            const unsigned char *val, unsigned int valLen,
                            unsigned char *out, unsigned int outLen)
{
    SECItem param = { siBuffer, NULL, 0 };
    CK_MECHANISM_TYPE mech = CKM_TLS_PRF_GENERAL;
    PK11Context *prf_context;
    unsigned int retLen;
    SECStatus rv;

    if (!spec->masterSecret) {
        PORT_Assert(spec->masterSecret);
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    if (spec->version >= SSL_LIBRARY_VERSION_TLS_1_2) {
        /* Bug 1312976 non-SHA256 exporters are broken. */
        if (ssl3_GetPrfHashMechanism(ss) != CKM_SHA256) {
            PORT_Assert(0);
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }
        mech = CKM_NSS_TLS_PRF_GENERAL_SHA256;
    }
    prf_context = PK11_CreateContextBySymKey(mech, CKA_SIGN,
                                             spec->masterSecret, &param);
    if (!prf_context)
        return SECFailure;

    rv = PK11_DigestBegin(prf_context);
    rv |= PK11_DigestOp(prf_context, (unsigned char *)label, labelLen);
    rv |= PK11_DigestOp(prf_context, val, valLen);
    rv |= PK11_DigestFinal(prf_context, out, &retLen, outLen);
    PORT_Assert(rv != SECSuccess || retLen == outLen);

    PK11_DestroyContext(prf_context, PR_TRUE);
    return rv;
}

/* called from ssl3_SendClientSecondRound
 *             ssl3_HandleFinished
 */
static SECStatus
ssl3_SendNextProto(sslSocket *ss)
{
    SECStatus rv;
    int padding_len;
    static const unsigned char padding[32] = { 0 };

    if (ss->xtnData.nextProto.len == 0 ||
        ss->xtnData.nextProtoState == SSL_NEXT_PROTO_SELECTED) {
        return SECSuccess;
    }

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    padding_len = 32 - ((ss->xtnData.nextProto.len + 2) % 32);

    rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_next_proto, ss->xtnData.nextProto.len + 2 + padding_len);
    if (rv != SECSuccess) {
        return rv; /* error code set by AppendHandshakeHeader */
    }
    rv = ssl3_AppendHandshakeVariable(ss, ss->xtnData.nextProto.data,
                                      ss->xtnData.nextProto.len, 1);
    if (rv != SECSuccess) {
        return rv; /* error code set by AppendHandshake */
    }
    rv = ssl3_AppendHandshakeVariable(ss, padding, padding_len, 1);
    if (rv != SECSuccess) {
        return rv; /* error code set by AppendHandshake */
    }
    return rv;
}

/* called from ssl3_SendFinished and tls13_DeriveSecret.
 *
 * This function is simply a debugging aid and therefore does not return a
 * SECStatus. */
void
ssl3_RecordKeyLog(sslSocket *ss, const char *label, PK11SymKey *secret)
{
#ifdef NSS_ALLOW_SSLKEYLOGFILE
    SECStatus rv;
    SECItem *keyData;
    /* Longest label is "CLIENT_HANDSHAKE_TRAFFIC_SECRET", master secret is 48
     * bytes which happens to be the largest in TLS 1.3 as well (SHA384).
     * Maximum line length: "CLIENT_HANDSHAKE_TRAFFIC_SECRET" (31) + " " (1) +
     * client_random (32*2) + " " (1) +
     * traffic_secret (48*2) + "\n" (1) = 194. */
    char buf[200];
    unsigned int offset, len;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (!ssl_keylog_iob)
        return;

    rv = PK11_ExtractKeyValue(secret);
    if (rv != SECSuccess)
        return;

    /* keyData does not need to be freed. */
    keyData = PK11_GetKeyData(secret);
    if (!keyData || !keyData->data)
        return;

    len = strlen(label) + 1 +          /* label + space */
          SSL3_RANDOM_LENGTH * 2 + 1 + /* client random (hex) + space */
          keyData->len * 2 + 1;        /* secret (hex) + newline */
    PORT_Assert(len <= sizeof(buf));
    if (len > sizeof(buf))
        return;

    /* https://developer.mozilla.org/en/NSS_Key_Log_Format */

    /* There could be multiple, concurrent writers to the
     * keylog, so we have to do everything in a single call to
     * fwrite. */

    strcpy(buf, label);
    offset = strlen(label);
    buf[offset++] += ' ';
    hexEncode(buf + offset, ss->ssl3.hs.client_random, SSL3_RANDOM_LENGTH);
    offset += SSL3_RANDOM_LENGTH * 2;
    buf[offset++] = ' ';
    hexEncode(buf + offset, keyData->data, keyData->len);
    offset += keyData->len * 2;
    buf[offset++] = '\n';

    PORT_Assert(offset == len);

    PZ_Lock(ssl_keylog_lock);
    if (fwrite(buf, len, 1, ssl_keylog_iob) == 1)
        fflush(ssl_keylog_iob);
    PZ_Unlock(ssl_keylog_lock);
#endif
}

/* called from ssl3_SendClientSecondRound
 *             ssl3_HandleClientHello
 *             ssl3_HandleFinished
 */
static SECStatus
ssl3_SendFinished(sslSocket *ss, PRInt32 flags)
{
    ssl3CipherSpec *cwSpec;
    PRBool isTLS;
    PRBool isServer = ss->sec.isServer;
    SECStatus rv;
    SSL3Sender sender = isServer ? sender_server : sender_client;
    SSL3Hashes hashes;
    TLSFinished tlsFinished;

    SSL_TRC(3, ("%d: SSL3[%d]: send finished handshake", SSL_GETPID(), ss->fd));

    PORT_Assert(ss->opt.noLocks || ssl_HaveXmitBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PR_ASSERT(!ss->ssl3.hs.clientCertificatePending);

    ssl_GetSpecReadLock(ss);
    cwSpec = ss->ssl3.cwSpec;
    isTLS = (PRBool)(cwSpec->version > SSL_LIBRARY_VERSION_3_0);
    rv = ssl3_ComputeHandshakeHashes(ss, cwSpec, &hashes, sender);
    if (isTLS && rv == SECSuccess) {
        rv = ssl3_ComputeTLSFinished(ss, cwSpec, isServer, &hashes, &tlsFinished);
    }
    ssl_ReleaseSpecReadLock(ss);
    if (rv != SECSuccess) {
        goto fail; /* err code was set by ssl3_ComputeHandshakeHashes */
    }

    if (isTLS) {
        if (isServer)
            ss->ssl3.hs.finishedMsgs.tFinished[1] = tlsFinished;
        else
            ss->ssl3.hs.finishedMsgs.tFinished[0] = tlsFinished;
        ss->ssl3.hs.finishedBytes = sizeof tlsFinished;
        rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_finished, sizeof tlsFinished);
        if (rv != SECSuccess)
            goto fail; /* err set by AppendHandshake. */
        rv = ssl3_AppendHandshake(ss, &tlsFinished, sizeof tlsFinished);
        if (rv != SECSuccess)
            goto fail; /* err set by AppendHandshake. */
    } else {
        if (isServer)
            ss->ssl3.hs.finishedMsgs.sFinished[1] = hashes.u.s;
        else
            ss->ssl3.hs.finishedMsgs.sFinished[0] = hashes.u.s;
        PORT_Assert(hashes.len == sizeof hashes.u.s);
        ss->ssl3.hs.finishedBytes = sizeof hashes.u.s;
        rv = ssl3_AppendHandshakeHeader(ss, ssl_hs_finished, sizeof hashes.u.s);
        if (rv != SECSuccess)
            goto fail; /* err set by AppendHandshake. */
        rv = ssl3_AppendHandshake(ss, &hashes.u.s, sizeof hashes.u.s);
        if (rv != SECSuccess)
            goto fail; /* err set by AppendHandshake. */
    }
    rv = ssl3_FlushHandshake(ss, flags);
    if (rv != SECSuccess) {
        goto fail; /* error code set by ssl3_FlushHandshake */
    }

    ssl3_RecordKeyLog(ss, "CLIENT_RANDOM", ss->ssl3.cwSpec->masterSecret);

    return SECSuccess;

fail:
    return rv;
}

/* wrap the master secret, and put it into the SID.
 * Caller holds the Spec read lock.
 */
SECStatus
ssl3_CacheWrappedSecret(sslSocket *ss, sslSessionID *sid,
                        PK11SymKey *secret)
{
    PK11SymKey *wrappingKey = NULL;
    PK11SlotInfo *symKeySlot;
    void *pwArg = ss->pkcs11PinArg;
    SECStatus rv = SECFailure;
    PRBool isServer = ss->sec.isServer;
    CK_MECHANISM_TYPE mechanism = CKM_INVALID_MECHANISM;

    symKeySlot = PK11_GetSlotFromKey(secret);
    if (!isServer) {
        int wrapKeyIndex;
        int incarnation;

        /* these next few functions are mere accessors and don't fail. */
        sid->u.ssl3.masterWrapIndex = wrapKeyIndex =
            PK11_GetCurrentWrapIndex(symKeySlot);
        PORT_Assert(wrapKeyIndex == 0); /* array has only one entry! */

        sid->u.ssl3.masterWrapSeries = incarnation =
            PK11_GetSlotSeries(symKeySlot);
        sid->u.ssl3.masterSlotID = PK11_GetSlotID(symKeySlot);
        sid->u.ssl3.masterModuleID = PK11_GetModuleID(symKeySlot);
        sid->u.ssl3.masterValid = PR_TRUE;
        /* Get the default wrapping key, for wrapping the master secret before
         * placing it in the SID cache entry. */
        wrappingKey = PK11_GetWrapKey(symKeySlot, wrapKeyIndex,
                                      CKM_INVALID_MECHANISM, incarnation,
                                      pwArg);
        if (wrappingKey) {
            mechanism = PK11_GetMechanism(wrappingKey); /* can't fail. */
        } else {
            int keyLength;
            /* if the wrappingKey doesn't exist, attempt to create it.
             * Note: we intentionally ignore errors here.  If we cannot
             * generate a wrapping key, it is not fatal to this SSL connection,
             * but we will not be able to restart this session.
             */
            mechanism = PK11_GetBestWrapMechanism(symKeySlot);
            keyLength = PK11_GetBestKeyLength(symKeySlot, mechanism);
            /* Zero length means fixed key length algorithm, or error.
             * It's ambiguous.
             */
            wrappingKey = PK11_KeyGen(symKeySlot, mechanism, NULL,
                                      keyLength, pwArg);
            if (wrappingKey) {
                /* The thread safety characteristics of PK11_[SG]etWrapKey is
                 * abominable.  This protects against races in calling
                 * PK11_SetWrapKey by dropping and re-acquiring the canonical
                 * value once it is set.  The mutex in PK11_[SG]etWrapKey will
                 * ensure that races produce the same value in the end. */
                PK11_SetWrapKey(symKeySlot, wrapKeyIndex, wrappingKey);
                PK11_FreeSymKey(wrappingKey);
                wrappingKey = PK11_GetWrapKey(symKeySlot, wrapKeyIndex,
                                              CKM_INVALID_MECHANISM, incarnation, pwArg);
                if (!wrappingKey) {
                    PK11_FreeSlot(symKeySlot);
                    return SECFailure;
                }
            }
        }
    } else {
        /* server socket using session cache. */
        mechanism = PK11_GetBestWrapMechanism(symKeySlot);
        if (mechanism != CKM_INVALID_MECHANISM) {
            wrappingKey =
                ssl3_GetWrappingKey(ss, symKeySlot, mechanism, pwArg);
            if (wrappingKey) {
                mechanism = PK11_GetMechanism(wrappingKey); /* can't fail. */
            }
        }
    }

    sid->u.ssl3.masterWrapMech = mechanism;
    PK11_FreeSlot(symKeySlot);

    if (wrappingKey) {
        SECItem wmsItem;

        wmsItem.data = sid->u.ssl3.keys.wrapped_master_secret;
        wmsItem.len = sizeof sid->u.ssl3.keys.wrapped_master_secret;
        rv = PK11_WrapSymKey(mechanism, NULL, wrappingKey,
                             secret, &wmsItem);
        /* rv is examined below. */
        sid->u.ssl3.keys.wrapped_master_secret_len = wmsItem.len;
        PK11_FreeSymKey(wrappingKey);
    }
    return rv;
}

/* Called from ssl3_HandlePostHelloHandshakeMessage() when it has deciphered
 * a complete ssl3 Finished message from the peer.
 * Caller must hold Handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleFinished(sslSocket *ss, PRUint8 *b, PRUint32 length)
{
    SECStatus rv = SECSuccess;
    PRBool isServer = ss->sec.isServer;
    PRBool isTLS;
    SSL3Hashes hashes;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(3, ("%d: SSL3[%d]: handle finished handshake",
                SSL_GETPID(), ss->fd));

    if (ss->ssl3.hs.ws != wait_finished) {
        SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_FINISHED);
        return SECFailure;
    }

    if (!ss->sec.isServer || !ss->opt.requestCertificate) {
        dtls_ReceivedFirstMessageInFlight(ss);
    }

    rv = ssl3_ComputeHandshakeHashes(ss, ss->ssl3.crSpec, &hashes,
                                     isServer ? sender_client : sender_server);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return SECFailure;
    }

    rv = ssl_HashHandshakeMessage(ss, ssl_hs_finished, b, length);
    if (rv != SECSuccess) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return rv;
    }

    isTLS = (PRBool)(ss->ssl3.crSpec->version > SSL_LIBRARY_VERSION_3_0);
    if (isTLS) {
        TLSFinished tlsFinished;

        if (length != sizeof(tlsFinished)) {
#ifndef UNSAFE_FUZZER_MODE
            (void)SSL3_SendAlert(ss, alert_fatal, decode_error);
            PORT_SetError(SSL_ERROR_RX_MALFORMED_FINISHED);
            return SECFailure;
#endif
        }
        rv = ssl3_ComputeTLSFinished(ss, ss->ssl3.crSpec, !isServer,
                                     &hashes, &tlsFinished);
        if (!isServer)
            ss->ssl3.hs.finishedMsgs.tFinished[1] = tlsFinished;
        else
            ss->ssl3.hs.finishedMsgs.tFinished[0] = tlsFinished;
        ss->ssl3.hs.finishedBytes = sizeof(tlsFinished);
        if (rv != SECSuccess ||
            0 != NSS_SecureMemcmp(&tlsFinished, b,
                                  PR_MIN(length, ss->ssl3.hs.finishedBytes))) {
#ifndef UNSAFE_FUZZER_MODE
            (void)SSL3_SendAlert(ss, alert_fatal, decrypt_error);
            PORT_SetError(SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE);
            return SECFailure;
#endif
        }
    } else {
        if (length != sizeof(SSL3Finished)) {
            (void)ssl3_IllegalParameter(ss);
            PORT_SetError(SSL_ERROR_RX_MALFORMED_FINISHED);
            return SECFailure;
        }

        if (!isServer)
            ss->ssl3.hs.finishedMsgs.sFinished[1] = hashes.u.s;
        else
            ss->ssl3.hs.finishedMsgs.sFinished[0] = hashes.u.s;
        PORT_Assert(hashes.len == sizeof hashes.u.s);
        ss->ssl3.hs.finishedBytes = sizeof hashes.u.s;
        if (0 != NSS_SecureMemcmp(&hashes.u.s, b, length)) {
            (void)ssl3_HandshakeFailure(ss);
            PORT_SetError(SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE);
            return SECFailure;
        }
    }

    ssl_GetXmitBufLock(ss); /*************************************/

    if ((isServer && !ss->ssl3.hs.isResuming) ||
        (!isServer && ss->ssl3.hs.isResuming)) {
        PRInt32 flags = 0;

        /* Send a NewSessionTicket message if the client sent us
         * either an empty session ticket, or one that did not verify.
         * (Note that if either of these conditions was met, then the
         * server has sent a SessionTicket extension in the
         * ServerHello message.)
         */
        if (isServer && !ss->ssl3.hs.isResuming &&
            ssl3_ExtensionNegotiated(ss, ssl_session_ticket_xtn) &&
            ssl3_KEASupportsTickets(ss->ssl3.hs.kea_def)) {
            /* RFC 5077 Section 3.3: "In the case of a full handshake, the
             * server MUST verify the client's Finished message before sending
             * the ticket." Presumably, this also means that the client's
             * certificate, if any, must be verified beforehand too.
             */
            rv = ssl3_SendNewSessionTicket(ss);
            if (rv != SECSuccess) {
                goto xmit_loser;
            }
        }

        rv = ssl3_SendChangeCipherSpecs(ss);
        if (rv != SECSuccess) {
            goto xmit_loser; /* err is set. */
        }
        /* If this thread is in SSL_SecureSend (trying to write some data)
        ** then set the ssl_SEND_FLAG_FORCE_INTO_BUFFER flag, so that the
        ** last two handshake messages (change cipher spec and finished)
        ** will be sent in the same send/write call as the application data.
        */
        if (ss->writerThread == PR_GetCurrentThread()) {
            flags = ssl_SEND_FLAG_FORCE_INTO_BUFFER;
        }

        if (!isServer && !ss->firstHsDone) {
            rv = ssl3_SendNextProto(ss);
            if (rv != SECSuccess) {
                goto xmit_loser; /* err code was set. */
            }
        }

        if (IS_DTLS(ss)) {
            flags |= ssl_SEND_FLAG_NO_RETRANSMIT;
        }

        rv = ssl3_SendFinished(ss, flags);
        if (rv != SECSuccess) {
            goto xmit_loser; /* err is set. */
        }
    }

xmit_loser:
    ssl_ReleaseXmitBufLock(ss); /*************************************/
    if (rv != SECSuccess) {
        return rv;
    }

    if (ss->ssl3.hs.authCertificatePending) {
        if (ss->ssl3.hs.restartTarget) {
            PR_NOT_REACHED("ssl3_HandleFinished: unexpected restartTarget");
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }

        ss->ssl3.hs.restartTarget = ssl3_FinishHandshake;
        PORT_SetError(PR_WOULD_BLOCK_ERROR);
        return SECFailure;
    }

    rv = ssl3_FinishHandshake(ss);
    return rv;
}

SECStatus
ssl3_FillInCachedSID(sslSocket *ss, sslSessionID *sid, PK11SymKey *secret)
{
    PORT_Assert(secret);

    /* fill in the sid */
    sid->u.ssl3.cipherSuite = ss->ssl3.hs.cipher_suite;
    sid->u.ssl3.policy = ss->ssl3.policy;
    sid->version = ss->version;
    sid->authType = ss->sec.authType;
    sid->authKeyBits = ss->sec.authKeyBits;
    sid->keaType = ss->sec.keaType;
    sid->keaKeyBits = ss->sec.keaKeyBits;
    if (ss->sec.keaGroup) {
        sid->keaGroup = ss->sec.keaGroup->name;
    } else {
        sid->keaGroup = ssl_grp_none;
    }
    sid->sigScheme = ss->sec.signatureScheme;
    sid->lastAccessTime = sid->creationTime = ssl_Time(ss);
    sid->expirationTime = sid->creationTime + (ssl_ticket_lifetime * PR_USEC_PER_SEC);
    if (sid->localCert) {
        CERT_DestroyCertificate(sid->localCert);
    }
    sid->localCert = CERT_DupCertificate(ss->sec.localCert);
    if (ss->sec.isServer) {
        sid->namedCurve = ss->sec.serverCert->namedCurve;
    }

    if (ss->xtnData.nextProtoState != SSL_NEXT_PROTO_NO_SUPPORT &&
        ss->xtnData.nextProto.data) {
        SECITEM_FreeItem(&sid->u.ssl3.alpnSelection, PR_FALSE);
        if (SECITEM_CopyItem(
                NULL, &sid->u.ssl3.alpnSelection, &ss->xtnData.nextProto) != SECSuccess) {
            return SECFailure; /* error already set. */
        }
    }

    /* Copy the master secret (wrapped or unwrapped) into the sid */
    return ssl3_CacheWrappedSecret(ss, ss->sec.ci.sid, secret);
}

/* The return type is SECStatus instead of void because this function needs
 * to have type sslRestartTarget.
 */
SECStatus
ssl3_FinishHandshake(sslSocket *ss)
{
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));
    PORT_Assert(ss->ssl3.hs.restartTarget == NULL);
    sslSessionID *sid = ss->sec.ci.sid;
    SECStatus sidRv = SECFailure;

    /* The first handshake is now completed. */
    ss->handshake = NULL;

    if (sid->cached == never_cached && !ss->opt.noCache) {
        /* If the wrap fails, don't cache the sid. The connection proceeds
         * normally, so the rv is only used to determine whether we cache. */
        sidRv = ssl3_FillInCachedSID(ss, sid, ss->ssl3.crSpec->masterSecret);
    }

    /* RFC 5077 Section 3.3: "The client MUST NOT treat the ticket as valid
     * until it has verified the server's Finished message." When the server
     * sends a NewSessionTicket in a resumption handshake, we must wait until
     * the handshake is finished (we have verified the server's Finished
     * AND the server's certificate) before we update the ticket in the sid.
     *
     * This must be done before we call ssl_CacheSessionID(ss)
     * because CacheSID requires the session ticket to already be set, and also
     * because of the lazy lock creation scheme used by CacheSID and
     * ssl3_SetSIDSessionTicket. */
    if (ss->ssl3.hs.receivedNewSessionTicket) {
        PORT_Assert(!ss->sec.isServer);
        if (sidRv == SECSuccess) {
            /* The sid takes over the ticket data */
            ssl3_SetSIDSessionTicket(ss->sec.ci.sid,
                                     &ss->ssl3.hs.newSessionTicket);
        } else {
            PORT_Assert(ss->ssl3.hs.newSessionTicket.ticket.data);
            SECITEM_FreeItem(&ss->ssl3.hs.newSessionTicket.ticket,
                             PR_FALSE);
        }
        PORT_Assert(!ss->ssl3.hs.newSessionTicket.ticket.data);
        ss->ssl3.hs.receivedNewSessionTicket = PR_FALSE;
    }
    if (sidRv == SECSuccess) {
        PORT_Assert(ss->sec.ci.sid->cached == never_cached);
        ssl_CacheSessionID(ss);
    }

    ss->ssl3.hs.canFalseStart = PR_FALSE; /* False Start phase is complete */
    ss->ssl3.hs.ws = idle_handshake;

    return ssl_FinishHandshake(ss);
}

SECStatus
ssl_HashHandshakeMessageInt(sslSocket *ss, SSLHandshakeType ct,
                            PRUint32 dtlsSeq,
                            const PRUint8 *b, PRUint32 length,
                            sslUpdateHandshakeHashes updateHashes)
{
    PRUint8 hdr[4];
    PRUint8 dtlsData[8];
    SECStatus rv;

    PRINT_BUF(50, (ss, "Hash handshake message:", b, length));

    hdr[0] = (PRUint8)ct;
    hdr[1] = (PRUint8)(length >> 16);
    hdr[2] = (PRUint8)(length >> 8);
    hdr[3] = (PRUint8)(length);

    rv = updateHashes(ss, (unsigned char *)hdr, 4);
    if (rv != SECSuccess)
        return rv; /* err code already set. */

    /* Extra data to simulate a complete DTLS handshake fragment */
    if (IS_DTLS_1_OR_12(ss)) {
        /* Sequence number */
        dtlsData[0] = MSB(dtlsSeq);
        dtlsData[1] = LSB(dtlsSeq);

        /* Fragment offset */
        dtlsData[2] = 0;
        dtlsData[3] = 0;
        dtlsData[4] = 0;

        /* Fragment length */
        dtlsData[5] = (PRUint8)(length >> 16);
        dtlsData[6] = (PRUint8)(length >> 8);
        dtlsData[7] = (PRUint8)(length);

        rv = updateHashes(ss, (unsigned char *)dtlsData, sizeof(dtlsData));
        if (rv != SECSuccess)
            return rv; /* err code already set. */
    }

    /* The message body */
    rv = updateHashes(ss, b, length);
    if (rv != SECSuccess)
        return rv; /* err code already set. */

    return SECSuccess;
}

SECStatus
ssl_HashHandshakeMessage(sslSocket *ss, SSLHandshakeType ct,
                         const PRUint8 *b, PRUint32 length)
{
    return ssl_HashHandshakeMessageInt(ss, ct, ss->ssl3.hs.recvMessageSeq,
                                       b, length, ssl3_UpdateHandshakeHashes);
}

SECStatus
ssl_HashHandshakeMessageDefault(sslSocket *ss, SSLHandshakeType ct,
                                const PRUint8 *b, PRUint32 length)
{
    return ssl_HashHandshakeMessageInt(ss, ct, ss->ssl3.hs.recvMessageSeq,
                                       b, length, ssl3_UpdateDefaultHandshakeHashes);
}
SECStatus
ssl_HashHandshakeMessageEchInner(sslSocket *ss, SSLHandshakeType ct,
                                 const PRUint8 *b, PRUint32 length)
{
    return ssl_HashHandshakeMessageInt(ss, ct, ss->ssl3.hs.recvMessageSeq,
                                       b, length, ssl3_UpdateInnerHandshakeHashes);
}

SECStatus
ssl_HashPostHandshakeMessage(sslSocket *ss, SSLHandshakeType ct,
                             const PRUint8 *b, PRUint32 length)
{
    return ssl_HashHandshakeMessageInt(ss, ct, ss->ssl3.hs.recvMessageSeq,
                                       b, length, ssl3_UpdatePostHandshakeHashes);
}

/* Called from ssl3_HandleHandshake() when it has gathered a complete ssl3
 * handshake message.
 * Caller must hold Handshake and RecvBuf locks.
 */
SECStatus
ssl3_HandleHandshakeMessage(sslSocket *ss, PRUint8 *b, PRUint32 length,
                            PRBool endOfRecord)
{
    SECStatus rv = SECSuccess;
    PRUint16 epoch;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    SSL_TRC(30, ("%d: SSL3[%d]: handle handshake message: %s", SSL_GETPID(),
                 ss->fd, ssl3_DecodeHandshakeType(ss->ssl3.hs.msg_type)));

    /* Start new handshake hashes when we start a new handshake. */
    if (ss->ssl3.hs.msg_type == ssl_hs_client_hello) {
        ssl3_RestartHandshakeHashes(ss);
    }
    switch (ss->ssl3.hs.msg_type) {
        case ssl_hs_hello_request:
        case ssl_hs_hello_verify_request:
            /* We don't include hello_request and hello_verify_request messages
             * in the handshake hashes */
            break;

        /* Defer hashing of these messages until the message handlers. */
        case ssl_hs_client_hello:
        case ssl_hs_server_hello:
        case ssl_hs_certificate_verify:
        case ssl_hs_finished:
            break;

        default:
            if (!tls13_IsPostHandshake(ss)) {
                rv = ssl_HashHandshakeMessage(ss, ss->ssl3.hs.msg_type, b, length);
                if (rv != SECSuccess) {
                    return SECFailure;
                }
            }
    }

    PORT_SetError(0); /* each message starts with no error. */

    if (ss->ssl3.hs.ws == wait_certificate_status &&
        ss->ssl3.hs.msg_type != ssl_hs_certificate_status) {
        /* If we negotiated the certificate_status extension then we deferred
         * certificate validation until we get the CertificateStatus messsage.
         * But the CertificateStatus message is optional. If the server did
         * not send it then we need to validate the certificate now. If the
         * server does send the CertificateStatus message then we will
         * authenticate the certificate in ssl3_HandleCertificateStatus.
         */
        rv = ssl3_AuthCertificate(ss); /* sets ss->ssl3.hs.ws */
        if (rv != SECSuccess) {
            /* This can't block. */
            PORT_Assert(PORT_GetError() != PR_WOULD_BLOCK_ERROR);
            return SECFailure;
        }
    }

    epoch = ss->ssl3.crSpec->epoch;
    switch (ss->ssl3.hs.msg_type) {
        case ssl_hs_client_hello:
            if (!ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CLIENT_HELLO);
                return SECFailure;
            }
            rv = ssl3_HandleClientHello(ss, b, length);
            break;
        case ssl_hs_server_hello:
            if (ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_SERVER_HELLO);
                return SECFailure;
            }
            rv = ssl3_HandleServerHello(ss, b, length);
            break;
        default:
            if (ss->version < SSL_LIBRARY_VERSION_TLS_1_3) {
                rv = ssl3_HandlePostHelloHandshakeMessage(ss, b, length);
            } else {
                rv = tls13_HandlePostHelloHandshakeMessage(ss, b, length);
            }
            break;
    }
    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
        (epoch != ss->ssl3.crSpec->epoch) && !endOfRecord) {
        /* If we changed read cipher states, there must not be any
         * data in the input queue. */
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HANDSHAKE);
        return SECFailure;
    }
    /* We consider the record to have been handled if SECSuccess or else WOULD_BLOCK is set
     * Whoever set WOULD_BLOCK must handle any remaining actions required to finsih processing the record.
     * e.g. by setting restartTarget.
     */
    if (IS_DTLS(ss) && (rv == SECSuccess || (rv == SECFailure && PR_GetError() == PR_WOULD_BLOCK_ERROR))) {
        /* Increment the expected sequence number */
        ss->ssl3.hs.recvMessageSeq++;
    }

    /* Taint the message so that it's easier to detect UAFs. */
    PORT_Memset(b, 'N', length);

    return rv;
}

static SECStatus
ssl3_HandlePostHelloHandshakeMessage(sslSocket *ss, PRUint8 *b,
                                     PRUint32 length)
{
    SECStatus rv;
    PORT_Assert(ss->version < SSL_LIBRARY_VERSION_TLS_1_3);

    switch (ss->ssl3.hs.msg_type) {
        case ssl_hs_hello_request:
            if (length != 0) {
                (void)ssl3_DecodeError(ss);
                PORT_SetError(SSL_ERROR_RX_MALFORMED_HELLO_REQUEST);
                return SECFailure;
            }
            if (ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HELLO_REQUEST);
                return SECFailure;
            }
            rv = ssl3_HandleHelloRequest(ss);
            break;

        case ssl_hs_hello_verify_request:
            if (!IS_DTLS(ss) || ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HELLO_VERIFY_REQUEST);
                return SECFailure;
            }
            rv = dtls_HandleHelloVerifyRequest(ss, b, length);
            break;
        case ssl_hs_certificate:
            rv = ssl3_HandleCertificate(ss, b, length);
            break;
        case ssl_hs_certificate_status:
            rv = ssl3_HandleCertificateStatus(ss, b, length);
            break;
        case ssl_hs_server_key_exchange:
            if (ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_SERVER_KEY_EXCH);
                return SECFailure;
            }
            rv = ssl3_HandleServerKeyExchange(ss, b, length);
            break;
        case ssl_hs_certificate_request:
            if (ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST);
                return SECFailure;
            }
            rv = ssl3_HandleCertificateRequest(ss, b, length);
            break;
        case ssl_hs_server_hello_done:
            if (length != 0) {
                (void)ssl3_DecodeError(ss);
                PORT_SetError(SSL_ERROR_RX_MALFORMED_HELLO_DONE);
                return SECFailure;
            }
            if (ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_HELLO_DONE);
                return SECFailure;
            }
            rv = ssl3_HandleServerHelloDone(ss);
            break;
        case ssl_hs_certificate_verify:
            if (!ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CERT_VERIFY);
                return SECFailure;
            }
            rv = ssl3_HandleCertificateVerify(ss, b, length);
            break;
        case ssl_hs_client_key_exchange:
            if (!ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_CLIENT_KEY_EXCH);
                return SECFailure;
            }
            rv = ssl3_HandleClientKeyExchange(ss, b, length);
            break;
        case ssl_hs_new_session_ticket:
            if (ss->sec.isServer) {
                (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
                PORT_SetError(SSL_ERROR_RX_UNEXPECTED_NEW_SESSION_TICKET);
                return SECFailure;
            }
            rv = ssl3_HandleNewSessionTicket(ss, b, length);
            break;
        case ssl_hs_finished:
            rv = ssl3_HandleFinished(ss, b, length);
            break;
        default:
            (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
            PORT_SetError(SSL_ERROR_RX_UNKNOWN_HANDSHAKE);
            rv = SECFailure;
    }

    return rv;
}

/* Called only from ssl3_HandleRecord, for each (deciphered) ssl3 record.
 * origBuf is the decrypted ssl record content.
 * Caller must hold the handshake and RecvBuf locks.
 */
static SECStatus
ssl3_HandleHandshake(sslSocket *ss, sslBuffer *origBuf)
{
    sslBuffer buf = *origBuf; /* Work from a copy. */
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));
    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    while (buf.len > 0) {
        if (ss->ssl3.hs.header_bytes < 4) {
            PRUint8 t;
            t = *(buf.buf++);
            buf.len--;
            if (ss->ssl3.hs.header_bytes++ == 0)
                ss->ssl3.hs.msg_type = (SSLHandshakeType)t;
            else
                ss->ssl3.hs.msg_len = (ss->ssl3.hs.msg_len << 8) + t;
            if (ss->ssl3.hs.header_bytes < 4)
                continue;

#define MAX_HANDSHAKE_MSG_LEN 0x1ffff /* 128k - 1 */
            if (ss->ssl3.hs.msg_len > MAX_HANDSHAKE_MSG_LEN) {
                (void)ssl3_DecodeError(ss);
                PORT_SetError(SSL_ERROR_RX_MALFORMED_HANDSHAKE);
                goto loser;
            }
#undef MAX_HANDSHAKE_MSG_LEN

            /* If msg_len is zero, be sure we fall through,
            ** even if buf.len is zero.
            */
            if (ss->ssl3.hs.msg_len > 0)
                continue;
        }

        /*
         * Header has been gathered and there is at least one byte of new
         * data available for this message. If it can be done right out
         * of the original buffer, then use it from there.
         */
        if (ss->ssl3.hs.msg_body.len == 0 && buf.len >= ss->ssl3.hs.msg_len) {
            /* handle it from input buffer */
            rv = ssl3_HandleHandshakeMessage(ss, buf.buf, ss->ssl3.hs.msg_len,
                                             buf.len == ss->ssl3.hs.msg_len);
            buf.buf += ss->ssl3.hs.msg_len;
            buf.len -= ss->ssl3.hs.msg_len;
            ss->ssl3.hs.msg_len = 0;
            ss->ssl3.hs.header_bytes = 0;
            if (rv != SECSuccess) {
                goto loser;
            }
        } else {
            /* must be copied to msg_body and dealt with from there */
            unsigned int bytes;

            PORT_Assert(ss->ssl3.hs.msg_body.len < ss->ssl3.hs.msg_len);
            bytes = PR_MIN(buf.len, ss->ssl3.hs.msg_len - ss->ssl3.hs.msg_body.len);

            /* Grow the buffer if needed */
            rv = sslBuffer_Grow(&ss->ssl3.hs.msg_body, ss->ssl3.hs.msg_len);
            if (rv != SECSuccess) {
                /* sslBuffer_Grow has set a memory error code. */
                goto loser;
            }

            PORT_Memcpy(ss->ssl3.hs.msg_body.buf + ss->ssl3.hs.msg_body.len,
                        buf.buf, bytes);
            ss->ssl3.hs.msg_body.len += bytes;
            buf.buf += bytes;
            buf.len -= bytes;

            PORT_Assert(ss->ssl3.hs.msg_body.len <= ss->ssl3.hs.msg_len);

            /* if we have a whole message, do it */
            if (ss->ssl3.hs.msg_body.len == ss->ssl3.hs.msg_len) {
                rv = ssl3_HandleHandshakeMessage(
                    ss, ss->ssl3.hs.msg_body.buf, ss->ssl3.hs.msg_len,
                    buf.len == 0);
                ss->ssl3.hs.msg_body.len = 0;
                ss->ssl3.hs.msg_len = 0;
                ss->ssl3.hs.header_bytes = 0;
                if (rv != SECSuccess) {
                    goto loser;
                }
            } else {
                PORT_Assert(buf.len == 0);
                break;
            }
        }
    } /* end loop */

    origBuf->len = 0; /* So ssl3_GatherAppDataRecord will keep looping. */
    return SECSuccess;

loser : {
    /* Make sure to remove any data that was consumed. */
    unsigned int consumed = origBuf->len - buf.len;
    PORT_Assert(consumed == buf.buf - origBuf->buf);
    if (consumed > 0) {
        memmove(origBuf->buf, origBuf->buf + consumed, buf.len);
        origBuf->len = buf.len;
    }
}
    return SECFailure;
}

/* SECStatusToMask returns, in constant time, a mask value of all ones if
 * rv == SECSuccess.  Otherwise it returns zero. */
static unsigned int
SECStatusToMask(SECStatus rv)
{
    return PORT_CT_EQ(rv, SECSuccess);
}

/* ssl_ConstantTimeGE returns 0xffffffff if a>=b and 0x00 otherwise. */
static unsigned char
ssl_ConstantTimeGE(unsigned int a, unsigned int b)
{
    return PORT_CT_GE(a, b);
}

/* ssl_ConstantTimeEQ returns 0xffffffff if a==b and 0x00 otherwise. */
static unsigned char
ssl_ConstantTimeEQ(unsigned char a, unsigned char b)
{
    return PORT_CT_EQ(a, b);
}

/* ssl_constantTimeSelect return a if mask is 0xFF and b if mask is 0x00 */
static unsigned char
ssl_constantTimeSelect(unsigned char mask, unsigned char a, unsigned char b)
{
    return (mask & a) | (~mask & b);
}

static SECStatus
ssl_RemoveSSLv3CBCPadding(sslBuffer *plaintext,
                          unsigned int blockSize,
                          unsigned int macSize)
{
    unsigned int paddingLength, good;
    const unsigned int overhead = 1 /* padding length byte */ + macSize;

    /* These lengths are all public so we can test them in non-constant
     * time. */
    if (overhead > plaintext->len) {
        return SECFailure;
    }

    paddingLength = plaintext->buf[plaintext->len - 1];
    /* SSLv3 padding bytes are random and cannot be checked. */
    good = PORT_CT_GE(plaintext->len, paddingLength + overhead);
    /* SSLv3 requires that the padding is minimal. */
    good &= PORT_CT_GE(blockSize, paddingLength + 1);
    plaintext->len -= good & (paddingLength + 1);
    return (good & SECSuccess) | (~good & SECFailure);
}

SECStatus
ssl_RemoveTLSCBCPadding(sslBuffer *plaintext, unsigned int macSize)
{
    unsigned int paddingLength, good, toCheck, i;
    const unsigned int overhead = 1 /* padding length byte */ + macSize;

    /* These lengths are all public so we can test them in non-constant
     * time. */
    if (overhead > plaintext->len) {
        return SECFailure;
    }

    paddingLength = plaintext->buf[plaintext->len - 1];
    good = PORT_CT_GE(plaintext->len, paddingLength + overhead);

    /* The padding consists of a length byte at the end of the record and then
     * that many bytes of padding, all with the same value as the length byte.
     * Thus, with the length byte included, there are paddingLength+1 bytes of
     * padding.
     *
     * We can't check just |paddingLength+1| bytes because that leaks
     * decrypted information. Therefore we always have to check the maximum
     * amount of padding possible. (Again, the length of the record is
     * public information so we can use it.) */
    toCheck = 256; /* maximum amount of padding + 1. */
    if (toCheck > plaintext->len) {
        toCheck = plaintext->len;
    }

    for (i = 0; i < toCheck; i++) {
        /* If i <= paddingLength then the MSB of t is zero and mask is
         * 0xff.  Otherwise, mask is 0. */
        unsigned char mask = PORT_CT_LE(i, paddingLength);
        unsigned char b = plaintext->buf[plaintext->len - 1 - i];
        /* The final |paddingLength+1| bytes should all have the value
         * |paddingLength|. Therefore the XOR should be zero. */
        good &= ~(mask & (paddingLength ^ b));
    }

    /* If any of the final |paddingLength+1| bytes had the wrong value,
     * one or more of the lower eight bits of |good| will be cleared. We
     * AND the bottom 8 bits together and duplicate the result to all the
     * bits. */
    good &= good >> 4;
    good &= good >> 2;
    good &= good >> 1;
    good <<= sizeof(good) * 8 - 1;
    good = PORT_CT_DUPLICATE_MSB_TO_ALL(good);

    plaintext->len -= good & (paddingLength + 1);
    return (good & SECSuccess) | (~good & SECFailure);
}

/* On entry:
 *   originalLength >= macSize
 *   macSize <= MAX_MAC_LENGTH
 *   plaintext->len >= macSize
 */
static void
ssl_CBCExtractMAC(sslBuffer *plaintext,
                  unsigned int originalLength,
                  PRUint8 *out,
                  unsigned int macSize)
{
    unsigned char rotatedMac[MAX_MAC_LENGTH];
    /* macEnd is the index of |plaintext->buf| just after the end of the
     * MAC. */
    unsigned macEnd = plaintext->len;
    unsigned macStart = macEnd - macSize;
    /* scanStart contains the number of bytes that we can ignore because
     * the MAC's position can only vary by 255 bytes. */
    unsigned scanStart = 0;
    unsigned i, j;
    unsigned char rotateOffset;

    if (originalLength > macSize + 255 + 1) {
        scanStart = originalLength - (macSize + 255 + 1);
    }

    /* We want to compute
     * rotateOffset = (macStart - scanStart) % macSize
     * But the time to compute this varies based on the amount of padding. Thus
     * we explicitely handle all mac sizes with (hopefully) constant time modulo
     * using Barrett reduction:
     *  q := (rotateOffset * m) >> k
     *  rotateOffset -= q * n
     *  if (n <= rotateOffset) rotateOffset -= n
     */
    rotateOffset = macStart - scanStart;
    /* rotateOffset < 255 + 1 + 48 = 304 */
    if (macSize == 16) {
        rotateOffset &= 15;
    } else if (macSize == 20) {
        /*
         * Correctness: rotateOffset * ( 1/20 - 25/2^9 ) < 1
         *              with rotateOffset <= 853
         */
        unsigned q = (rotateOffset * 25) >> 9;
        rotateOffset -= q * 20;
        rotateOffset -= ssl_constantTimeSelect(ssl_ConstantTimeGE(rotateOffset, 20),
                                               20, 0);
    } else if (macSize == 32) {
        rotateOffset &= 31;
    } else if (macSize == 48) {
        /*
         * Correctness: rotateOffset * ( 1/48 - 10/2^9 ) < 1
         *              with rotateOffset < 768
         */
        unsigned q = (rotateOffset * 10) >> 9;
        rotateOffset -= q * 48;
        rotateOffset -= ssl_constantTimeSelect(ssl_ConstantTimeGE(rotateOffset, 48),
                                               48, 0);
    } else {
        /*
         * SHA384 (macSize == 48) is the largest we support. We should never
         * get here.
         */
        PORT_Assert(0);
        rotateOffset = rotateOffset % macSize;
    }

    memset(rotatedMac, 0, macSize);
    for (i = scanStart; i < originalLength;) {
        for (j = 0; j < macSize && i < originalLength; i++, j++) {
            unsigned char macStarted = ssl_ConstantTimeGE(i, macStart);
            unsigned char macEnded = ssl_ConstantTimeGE(i, macEnd);
            unsigned char b = 0;
            b = plaintext->buf[i];
            rotatedMac[j] |= b & macStarted & ~macEnded;
        }
    }

    /* Now rotate the MAC. If we knew that the MAC fit into a CPU cache line
     * we could line-align |rotatedMac| and rotate in place. */
    memset(out, 0, macSize);
    rotateOffset = macSize - rotateOffset;
    rotateOffset = ssl_constantTimeSelect(ssl_ConstantTimeGE(rotateOffset, macSize),
                                          0, rotateOffset);
    for (i = 0; i < macSize; i++) {
        for (j = 0; j < macSize; j++) {
            out[j] |= rotatedMac[i] & ssl_ConstantTimeEQ(j, rotateOffset);
        }
        rotateOffset++;
        rotateOffset = ssl_constantTimeSelect(ssl_ConstantTimeGE(rotateOffset, macSize),
                                              0, rotateOffset);
    }
}

/* MAX_EXPANSION is the amount by which a record might plausibly be expanded
 * when protected.  It's the worst case estimate, so the sum of block cipher
 * padding (up to 256 octets), HMAC (48 octets for SHA-384), and IV (16
 * octets for AES). */
#define MAX_EXPANSION (256 + 48 + 16)

/* Unprotect an SSL3 record and leave the result in plaintext.
 *
 * If SECFailure is returned, we:
 * 1. Set |*alert| to the alert to be sent.
 * 2. Call PORT_SetError() with an appropriate code.
 *
 * Called by ssl3_HandleRecord. Caller must hold the spec read lock.
 * Therefore, we MUST not call SSL3_SendAlert().
 *
 */
static SECStatus
ssl3_UnprotectRecord(sslSocket *ss,
                     ssl3CipherSpec *spec,
                     SSL3Ciphertext *cText, sslBuffer *plaintext,
                     SSL3AlertDescription *alert)
{
    const ssl3BulkCipherDef *cipher_def = spec->cipherDef;
    PRBool isTLS;
    unsigned int good;
    unsigned int ivLen = 0;
    SSLContentType rType;
    SSL3ProtocolVersion rVersion;
    unsigned int minLength;
    unsigned int originalLen = 0;
    PRUint8 headerBuf[13];
    sslBuffer header = SSL_BUFFER(headerBuf);
    PRUint8 hash[MAX_MAC_LENGTH];
    PRUint8 givenHashBuf[MAX_MAC_LENGTH];
    PRUint8 *givenHash;
    unsigned int hashBytes = MAX_MAC_LENGTH + 1;
    SECStatus rv;

    PORT_Assert(spec->direction == ssl_secret_read);

    good = ~0U;
    minLength = spec->macDef->mac_size;
    if (cipher_def->type == type_block) {
        /* CBC records have a padding length byte at the end. */
        minLength++;
        if (spec->version >= SSL_LIBRARY_VERSION_TLS_1_1) {
            /* With >= TLS 1.1, CBC records have an explicit IV. */
            minLength += cipher_def->iv_size;
        }
    } else if (cipher_def->type == type_aead) {
        minLength = cipher_def->explicit_nonce_size + cipher_def->tag_size;
    }

    /* We can perform this test in variable time because the record's total
     * length and the ciphersuite are both public knowledge. */
    if (cText->buf->len < minLength) {
        goto decrypt_loser;
    }

    if (cipher_def->type == type_block &&
        spec->version >= SSL_LIBRARY_VERSION_TLS_1_1) {
        /* Consume the per-record explicit IV. RFC 4346 Section 6.2.3.2 states
         * "The receiver decrypts the entire GenericBlockCipher structure and
         * then discards the first cipher block corresponding to the IV
         * component." Instead, we decrypt the first cipher block and then
         * discard it before decrypting the rest.
         */
        PRUint8 iv[MAX_IV_LENGTH];
        unsigned int decoded;

        ivLen = cipher_def->iv_size;
        if (ivLen < 8 || ivLen > sizeof(iv)) {
            *alert = internal_error;
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            return SECFailure;
        }

        PRINT_BUF(80, (ss, "IV (ciphertext):", cText->buf->buf, ivLen));

        /* The decryption result is garbage, but since we just throw away
         * the block it doesn't matter.  The decryption of the next block
         * depends only on the ciphertext of the IV block.
         */
        rv = spec->cipher(spec->cipherContext, iv, &decoded,
                          sizeof(iv), cText->buf->buf, ivLen);

        good &= SECStatusToMask(rv);
    }

    PRINT_BUF(80, (ss, "ciphertext:", cText->buf->buf + ivLen,
                   cText->buf->len - ivLen));

    /* Check if the ciphertext can be valid if we assume maximum plaintext and
     * add the maximum possible ciphersuite expansion.
     * This way we detect overlong plaintexts/padding before decryption.
     * This check enforces size limitations more strict than the RFC.
     * [RFC5246, Section 6.2.3] */
    if (cText->buf->len > (spec->recordSizeLimit + MAX_EXPANSION)) {
        *alert = record_overflow;
        PORT_SetError(SSL_ERROR_RX_RECORD_TOO_LONG);
        return SECFailure;
    }

    isTLS = (PRBool)(spec->version > SSL_LIBRARY_VERSION_3_0);
    rType = (SSLContentType)cText->hdr[0];
    rVersion = ((SSL3ProtocolVersion)cText->hdr[1] << 8) |
               (SSL3ProtocolVersion)cText->hdr[2];
    if (cipher_def->type == type_aead) {
        /* XXX For many AEAD ciphers, the plaintext is shorter than the
         * ciphertext by a fixed byte count, but it is not true in general.
         * Each AEAD cipher should provide a function that returns the
         * plaintext length for a given ciphertext. */
        const unsigned int explicitNonceLen = cipher_def->explicit_nonce_size;
        const unsigned int tagLen = cipher_def->tag_size;
        unsigned int nonceLen = explicitNonceLen;
        unsigned int decryptedLen = cText->buf->len - nonceLen - tagLen;
        /* even though read doesn't return and IV, we still need a space to put
         * the combined iv/nonce n the gcm 1.2 case*/
        unsigned char ivOut[MAX_IV_LENGTH];
        unsigned char *iv = NULL;
        unsigned char *nonce = NULL;

        ivLen = cipher_def->iv_size;

        rv = ssl3_BuildRecordPseudoHeader(
            spec->epoch, cText->seqNum,
            rType, isTLS, rVersion, IS_DTLS(ss), decryptedLen, &header, spec->version);
        PORT_Assert(rv == SECSuccess);

        /* build the iv */
        if (explicitNonceLen == 0) {
            nonceLen = sizeof(cText->seqNum);
            iv = spec->keyMaterial.iv;
            nonce = SSL_BUFFER_BASE(&header);
        } else {
            PORT_Memcpy(ivOut, spec->keyMaterial.iv, ivLen);
            PORT_Memset(ivOut + ivLen, 0, explicitNonceLen);
            iv = ivOut;
            nonce = cText->buf->buf;
            nonceLen = explicitNonceLen;
        }
        rv = tls13_AEAD(spec->cipherContext, PR_TRUE,
                        CKG_NO_GENERATE, 0,       /* iv generator params
                                                   * (not used in decrypt)*/
                        iv,                       /* iv in */
                        NULL,                     /* iv out */
                        ivLen + explicitNonceLen, /* full iv length */
                        nonce, nonceLen,          /* nonce in */
                        SSL_BUFFER_BASE(&header), /* aad */
                        SSL_BUFFER_LEN(&header),  /* aadlen */
                        plaintext->buf,           /* output  */
                        &plaintext->len,          /* out len */
                        plaintext->space,         /* max out */
                        tagLen,
                        cText->buf->buf + explicitNonceLen,  /* input */
                        cText->buf->len - explicitNonceLen); /* input len */
        if (rv != SECSuccess) {
            good = 0;
        }
    } else {
        if (cipher_def->type == type_block &&
            ((cText->buf->len - ivLen) % cipher_def->block_size) != 0) {
            goto decrypt_loser;
        }

        /* decrypt from cText buf to plaintext. */
        rv = spec->cipher(
            spec->cipherContext, plaintext->buf, &plaintext->len,
            plaintext->space, cText->buf->buf + ivLen, cText->buf->len - ivLen);
        if (rv != SECSuccess) {
            goto decrypt_loser;
        }

        PRINT_BUF(80, (ss, "cleartext:", plaintext->buf, plaintext->len));

        originalLen = plaintext->len;

        /* If it's a block cipher, check and strip the padding. */
        if (cipher_def->type == type_block) {
            const unsigned int blockSize = cipher_def->block_size;
            const unsigned int macSize = spec->macDef->mac_size;

            if (!isTLS) {
                good &= SECStatusToMask(ssl_RemoveSSLv3CBCPadding(
                    plaintext, blockSize, macSize));
            } else {
                good &= SECStatusToMask(ssl_RemoveTLSCBCPadding(
                    plaintext, macSize));
            }
        }

        /* compute the MAC */
        rv = ssl3_BuildRecordPseudoHeader(
            spec->epoch, cText->seqNum,
            rType, isTLS, rVersion, IS_DTLS(ss),
            plaintext->len - spec->macDef->mac_size, &header, spec->version);
        PORT_Assert(rv == SECSuccess);
        if (cipher_def->type == type_block) {
            rv = ssl3_ComputeRecordMACConstantTime(
                spec, SSL_BUFFER_BASE(&header), SSL_BUFFER_LEN(&header),
                plaintext->buf, plaintext->len, originalLen,
                hash, &hashBytes);

            ssl_CBCExtractMAC(plaintext, originalLen, givenHashBuf,
                              spec->macDef->mac_size);
            givenHash = givenHashBuf;

            /* plaintext->len will always have enough space to remove the MAC
             * because in ssl_Remove{SSLv3|TLS}CBCPadding we only adjust
             * plaintext->len if the result has enough space for the MAC and we
             * tested the unadjusted size against minLength, above. */
            plaintext->len -= spec->macDef->mac_size;
        } else {
            /* This is safe because we checked the minLength above. */
            plaintext->len -= spec->macDef->mac_size;

            rv = ssl3_ComputeRecordMAC(
                spec, SSL_BUFFER_BASE(&header), SSL_BUFFER_LEN(&header),
                plaintext->buf, plaintext->len, hash, &hashBytes);

            /* We can read the MAC directly from the record because its location
             * is public when a stream cipher is used. */
            givenHash = plaintext->buf + plaintext->len;
        }

        good &= SECStatusToMask(rv);

        if (hashBytes != (unsigned)spec->macDef->mac_size ||
            NSS_SecureMemcmp(givenHash, hash, spec->macDef->mac_size) != 0) {
            /* We're allowed to leak whether or not the MAC check was correct */
            good = 0;
        }
    }

    if (good == 0) {
    decrypt_loser:
        /* always log mac error, in case attacker can read server logs. */
        PORT_SetError(SSL_ERROR_BAD_MAC_READ);
        *alert = bad_record_mac;
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
ssl3_HandleNonApplicationData(sslSocket *ss, SSLContentType rType,
                              DTLSEpoch epoch, sslSequenceNumber seqNum,
                              sslBuffer *databuf)
{
    SECStatus rv;

    /* check for Token Presence */
    if (!ssl3_ClientAuthTokenPresent(ss->sec.ci.sid)) {
        PORT_SetError(SSL_ERROR_TOKEN_INSERTION_REMOVAL);
        return SECFailure;
    }

    ssl_GetSSL3HandshakeLock(ss);

    /* All the functions called in this switch MUST set error code if
    ** they return SECFailure.
    */
    switch (rType) {
        case ssl_ct_change_cipher_spec:
            rv = ssl3_HandleChangeCipherSpecs(ss, databuf);
            break;
        case ssl_ct_alert:
            rv = ssl3_HandleAlert(ss, databuf);
            break;
        case ssl_ct_handshake:
            if (!IS_DTLS(ss)) {
                rv = ssl3_HandleHandshake(ss, databuf);
            } else {
                rv = dtls_HandleHandshake(ss, epoch, seqNum, databuf);
            }
            break;
        case ssl_ct_ack:
            if (IS_DTLS(ss) && tls13_MaybeTls13(ss)) {
                rv = dtls13_HandleAck(ss, databuf);
                break;
            }
        /* Fall through. */
        default:
            /* If a TLS implementation receives an unexpected record type,
             * it MUST terminate the connection with an "unexpected_message"
             * alert [RFC8446, Section 5].
             *
             * For TLS 1.3 the outer content type is checked before in
             * tls13con.c/tls13_UnprotectRecord(),
             * For DTLS 1.3 the outer content type is checked before in
             * ssl3gthr.c/dtls_GatherData.
             * The inner content types will be checked here.
             *
             * In DTLS generally invalid records SHOULD be silently discarded,
             * no alert is sent [RFC6347, Section 4.1.2.7].
             */
            if (!IS_DTLS(ss)) {
                SSL3_SendAlert(ss, alert_fatal, unexpected_message);
            }
            PORT_SetError(SSL_ERROR_RX_UNKNOWN_RECORD_TYPE);
            SSL_DBG(("%d: SSL3[%d]: bogus content type=%d",
                     SSL_GETPID(), ss->fd, rType));
            rv = SECFailure;
            break;
    }

    ssl_ReleaseSSL3HandshakeLock(ss);
    return rv;
}

/* Find the cipher spec to use for a given record. For TLS, this
 * is the current cipherspec. For DTLS, we look up by epoch.
 * In DTLS < 1.3 this just means the current epoch or nothing,
 * but in DTLS >= 1.3, we keep multiple reading cipherspecs.
 * Returns NULL if no appropriate cipher spec is found.
 */
static ssl3CipherSpec *
ssl3_GetCipherSpec(sslSocket *ss, SSL3Ciphertext *cText)
{
    ssl3CipherSpec *crSpec = ss->ssl3.crSpec;
    ssl3CipherSpec *newSpec = NULL;
    DTLSEpoch epoch;

    if (!IS_DTLS(ss)) {
        return crSpec;
    }
    epoch = dtls_ReadEpoch(crSpec->version, crSpec->epoch, cText->hdr);
    if (crSpec->epoch == epoch) {
        return crSpec;
    }
    if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        /* Try to find the cipher spec. */
        newSpec = ssl_FindCipherSpecByEpoch(ss, ssl_secret_read,
                                            epoch);
        if (newSpec != NULL) {
            return newSpec;
        }
    }
    SSL_TRC(10, ("%d: DTLS[%d]: %s couldn't find cipherspec from epoch %d",
                 SSL_GETPID(), ss->fd, SSL_ROLE(ss), epoch));
    return NULL;
}

/* if cText is non-null, then decipher and check the MAC of the
 * SSL record from cText->buf (typically gs->inbuf)
 * into databuf (typically gs->buf), and any previous contents of databuf
 * is lost.  Then handle databuf according to its SSL record type,
 * unless it's an application record.
 *
 * If cText is NULL, then the ciphertext has previously been deciphered and
 * checked, and is already sitting in databuf.  It is processed as an SSL
 * Handshake message.
 *
 * DOES NOT process the decrypted application data.
 * On return, databuf contains the decrypted record.
 *
 * Called from ssl3_GatherCompleteHandshake
 *             ssl3_RestartHandshakeAfterCertReq
 *
 * Caller must hold the RecvBufLock.
 *
 * This function aquires and releases the SSL3Handshake Lock, holding the
 * lock around any calls to functions that handle records other than
 * Application Data records.
 */
SECStatus
ssl3_HandleRecord(sslSocket *ss, SSL3Ciphertext *cText)
{
    SECStatus rv = SECFailure;
    PRBool isTLS, isTLS13;
    DTLSEpoch epoch;
    ssl3CipherSpec *spec = NULL;
    PRUint16 recordSizeLimit, cTextSizeLimit;
    PRBool outOfOrderSpec = PR_FALSE;
    SSLContentType rType;
    sslBuffer *plaintext = &ss->gs.buf;
    SSL3AlertDescription alert = internal_error;
    PORT_Assert(ss->opt.noLocks || ssl_HaveRecvBufLock(ss));

    /* check for Token Presence */
    if (!ssl3_ClientAuthTokenPresent(ss->sec.ci.sid)) {
        PORT_SetError(SSL_ERROR_TOKEN_INSERTION_REMOVAL);
        return SECFailure;
    }

    /* Clear out the buffer in case this exits early.  Any data then won't be
     * processed twice. */
    plaintext->len = 0;

    /* We're waiting for another ClientHello, which will appear unencrypted.
     * Use the content type to tell whether this should be discarded. */
    if (ss->ssl3.hs.zeroRttIgnore == ssl_0rtt_ignore_hrr &&
        cText->hdr[0] == ssl_ct_application_data) {
        PORT_Assert(ss->ssl3.hs.ws == wait_client_hello);
        return SECSuccess;
    }

    ssl_GetSpecReadLock(ss); /******************************************/
    spec = ssl3_GetCipherSpec(ss, cText);
    if (!spec) {
        PORT_Assert(IS_DTLS(ss));
        ssl_ReleaseSpecReadLock(ss); /*****************************/
        return SECSuccess;
    }
    if (spec != ss->ssl3.crSpec) {
        PORT_Assert(IS_DTLS(ss));
        SSL_TRC(3, ("%d: DTLS[%d]: Handling out-of-epoch record from epoch=%d",
                    SSL_GETPID(), ss->fd, spec->epoch));
        outOfOrderSpec = PR_TRUE;
    }
    isTLS = (PRBool)(spec->version > SSL_LIBRARY_VERSION_3_0);
    if (IS_DTLS(ss)) {
        if (dtls13_MaskSequenceNumber(ss, spec, cText->hdr,
                                      SSL_BUFFER_BASE(cText->buf), SSL_BUFFER_LEN(cText->buf)) != SECSuccess) {
            ssl_ReleaseSpecReadLock(ss); /*****************************/
            /* code already set. */
            return SECFailure;
        }
        if (!dtls_IsRelevant(ss, spec, cText, &cText->seqNum)) {
            ssl_ReleaseSpecReadLock(ss); /*****************************/
            return SECSuccess;
        }
    } else {
        cText->seqNum = spec->nextSeqNum;
    }
    if (cText->seqNum >= spec->cipherDef->max_records) {
        ssl_ReleaseSpecReadLock(ss); /*****************************/
        SSL_TRC(3, ("%d: SSL[%d]: read sequence number at limit 0x%0llx",
                    SSL_GETPID(), ss->fd, cText->seqNum));
        PORT_SetError(SSL_ERROR_TOO_MANY_RECORDS);
        return SECFailure;
    }

    isTLS13 = (PRBool)(ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
    recordSizeLimit = spec->recordSizeLimit;
    cTextSizeLimit = recordSizeLimit;
    cTextSizeLimit += (isTLS13) ? TLS_1_3_MAX_EXPANSION : TLS_1_2_MAX_EXPANSION;

    /* Check if the specified recordSizeLimit and the RFC8446 specified max
     * expansion are respected. recordSizeLimit is probably at the default for
     * the first (hello) handshake message and then set to a smaller size by
     * the Record Size Limit Extension.
     * Stricter expansion size checks dependent on implemented cipher suites
     * are performed in ssl3con.c/ssl3_UnprotectRecord() OR
     * tls13con.c/tls13_UnprotextRecord().
     * After Decryption the plaintext size is checked (l. 13424). This also
     * applies to unencrypted records. */
    if (cText->buf->len > cTextSizeLimit) {
        ssl_ReleaseSpecReadLock(ss); /*****************************/
        /* Drop DTLS Record Errors silently [RFC6347, Section 4.1.2.7] */
        if (IS_DTLS(ss)) {
            return SECSuccess;
        }
        SSL3_SendAlert(ss, alert_fatal, record_overflow);
        PORT_SetError(SSL_ERROR_RX_RECORD_TOO_LONG);
        return SECFailure;
    }

#ifdef DEBUG
    /* In debug builds the gather buffers are freed after the handling of each
     * record for advanced ASAN coverage. Allocate the buffer again to the
     * maximum possibly needed size as on gather initialization in
     * ssl3gthr.c/ssl3_InitGather(). */
    PR_ASSERT(sslBuffer_Grow(plaintext, TLS_1_2_MAX_CTEXT_LENGTH) == SECSuccess);
#endif
    /* This replaces a dynamic plaintext buffer size check, since the buffer is
     * allocated to the maximum size in ssl3gthr.c/ssl3_InitGather(). The buffer
     * was always grown to the maximum size at first record gathering before. */
    PR_ASSERT(plaintext->space >= cTextSizeLimit);

    /* Most record types aside from protected TLS 1.3 records carry the content
     * type in the first octet. TLS 1.3 will override this value later. */
    rType = cText->hdr[0];
    /* Encrypted application data records could arrive before the handshake
     * completes in DTLS 1.3. These can look like valid TLS 1.2 application_data
     * records in epoch 0, which is never valid. Pretend they didn't decrypt. */
    if (spec->epoch == 0 && ((IS_DTLS(ss) &&
                              dtls_IsDtls13Ciphertext(0, rType)) ||
                             rType == ssl_ct_application_data)) {
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_APPLICATION_DATA);
        alert = unexpected_message;
        rv = SECFailure;
    } else {
#ifdef UNSAFE_FUZZER_MODE
        rv = Null_Cipher(NULL, plaintext->buf, &plaintext->len,
                         plaintext->space, cText->buf->buf, cText->buf->len);
#else
        /* IMPORTANT:
         * Unprotect functions MUST NOT send alerts
         * because we still hold the spec read lock. Instead, if they
         * return SECFailure, they set *alert to the alert to be sent.
         * Additionaly, this is used to silently drop DTLS encryption/record
         * errors/alerts using the error handling below as suggested in the
         * DTLS specification [RFC6347, Section 4.1.2.7]. */
        if (spec->cipherDef->cipher == cipher_null && cText->buf->len == 0) {
            /* Handle a zero-length unprotected record
             * In this case, we treat it as a no-op and let later functions decide
             * whether to ignore or alert accordingly. */
            PR_ASSERT(plaintext->len == 0);
            rv = SECSuccess;
        } else if (spec->version < SSL_LIBRARY_VERSION_TLS_1_3 || spec->epoch == 0) {
            rv = ssl3_UnprotectRecord(ss, spec, cText, plaintext, &alert);
        } else {
            rv = tls13_UnprotectRecord(ss, spec, cText, plaintext, &rType,
                                       &alert);
        }
#endif
    }

    /* Error/Alert handling for ssl3/tls13_UnprotectRecord */
    if (rv != SECSuccess) {
        ssl_ReleaseSpecReadLock(ss); /***************************/

        SSL_DBG(("%d: SSL3[%d]: decryption failed", SSL_GETPID(), ss->fd));

        /* Ensure that we don't process this data again. */
        plaintext->len = 0;

        /* Ignore a CCS if compatibility mode is negotiated.  Note that this
         * will fail if the server fails to negotiate compatibility mode in a
         * 0-RTT session that is resumed from a session that did negotiate it.
         * We don't care about that corner case right now. */
        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
            cText->hdr[0] == ssl_ct_change_cipher_spec &&
            ss->ssl3.hs.ws != idle_handshake &&
            cText->buf->len == 1 &&
            cText->buf->buf[0] == change_cipher_spec_choice) {
            if (!ss->ssl3.hs.rejectCcs) {
                /* Allow only the first CCS. */
                ss->ssl3.hs.rejectCcs = PR_TRUE;
                return SECSuccess;
            } else {
                alert = unexpected_message;
                PORT_SetError(SSL_ERROR_RX_MALFORMED_CHANGE_CIPHER);
            }
        }

        /* All errors/alerts that might occur during unprotection are related
         * to invalid records (e.g. invalid formatting, length, MAC, ...).
         * Following the DTLS specification such errors/alerts SHOULD be
         * dropped silently [RFC9147, Section 4.5.2].
         * This is done below. */

        if ((IS_DTLS(ss) && !dtls13_AeadLimitReached(spec)) ||
            (!IS_DTLS(ss) && ss->sec.isServer &&
             ss->ssl3.hs.zeroRttIgnore == ssl_0rtt_ignore_trial)) {
            /* Silently drop the packet unless we set ss->ssl3.fatalAlertSent.
             * (Manually or by using functions like
             * SSL3_SendAlert(.., alert_fatal,..))
             * This is not currently used in the unprotection functions since
             * all TLS and DTLS errors are propagated to this handler. */
            if (ss->ssl3.fatalAlertSent) {
                return SECFailure;
            }
            return SECSuccess;
        }

        int errCode = PORT_GetError();
        SSL3_SendAlert(ss, alert_fatal, alert);
        /* Reset the error code in case SSL3_SendAlert called
         * PORT_SetError(). */
        PORT_SetError(errCode);
        return SECFailure;
    }

    /* SECSuccess */
    if (IS_DTLS(ss)) {
        dtls_RecordSetRecvd(&spec->recvdRecords, cText->seqNum);
        spec->nextSeqNum = PR_MAX(spec->nextSeqNum, cText->seqNum + 1);
    } else {
        ++spec->nextSeqNum;
    }
    epoch = spec->epoch;

    ssl_ReleaseSpecReadLock(ss); /*****************************************/

    /*
     * The decrypted data is now in plaintext.
     */

    /* IMPORTANT: We are in DTLS 1.3 mode and we have processed something
     * from the wrong epoch. Divert to a divert processing function to make
     * sure we don't accidentally use the data unsafely. */

    /* We temporary allowed reading the records from the previous epoch n-1
    until the moment we get a message from the new epoch n. */

    if (outOfOrderSpec) {
        PORT_Assert(IS_DTLS(ss) && ss->version >= SSL_LIBRARY_VERSION_TLS_1_3);
        ssl_GetSSL3HandshakeLock(ss);
        if (ss->ssl3.hs.allowPreviousEpoch && spec->epoch == ss->ssl3.crSpec->epoch - 1) {
            SSL_TRC(30, ("%d: DTLS13[%d]: Out of order message %d is accepted",
                         SSL_GETPID(), ss->fd, spec->epoch));
            ssl_ReleaseSSL3HandshakeLock(ss);
        } else {
            ssl_ReleaseSSL3HandshakeLock(ss);
            return dtls13_HandleOutOfEpochRecord(ss, spec, rType, plaintext);
        }
    } else {
        ssl_GetSSL3HandshakeLock(ss);
        /* Forbid (application) messages from the previous epoch.
           From now, messages that arrive out of order will be discarded. */
        ss->ssl3.hs.allowPreviousEpoch = PR_FALSE;
        ssl_ReleaseSSL3HandshakeLock(ss);
    }

    /* Check the length of the plaintext. */
    if (isTLS && plaintext->len > recordSizeLimit) {
        plaintext->len = 0;
        /* Drop DTLS Record Errors silently [RFC6347, Section 4.1.2.7] */
        if (IS_DTLS(ss)) {
            return SECSuccess;
        }
        SSL3_SendAlert(ss, alert_fatal, record_overflow);
        PORT_SetError(SSL_ERROR_RX_RECORD_TOO_LONG);
        return SECFailure;
    }

    /* Application data records are processed by the caller of this
    ** function, not by this function.
    */
    if (rType == ssl_ct_application_data) {
        if (ss->firstHsDone)
            return SECSuccess;
        if (ss->version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
            ss->sec.isServer &&
            ss->ssl3.hs.zeroRttState == ssl_0rtt_accepted) {
            return tls13_HandleEarlyApplicationData(ss, plaintext);
        }
        plaintext->len = 0;
        (void)SSL3_SendAlert(ss, alert_fatal, unexpected_message);
        PORT_SetError(SSL_ERROR_RX_UNEXPECTED_APPLICATION_DATA);
        return SECFailure;
    }

    rv = ssl3_HandleNonApplicationData(ss, rType, epoch, cText->seqNum,
                                       plaintext);

#ifdef DEBUG
    /* In Debug builds free and zero gather plaintext buffer after its content
     * has been used/copied for advanced ASAN coverage/utilization.
     * This frees buffer for non application data records, for application data
     * records it is freed in sslsecur.c/DoRecv(). */
    sslBuffer_Clear(&ss->gs.buf);
#endif

    return rv;
}

/*
 * Initialization functions
 */

void
ssl_InitSecState(sslSecurityInfo *sec)
{
    sec->authType = ssl_auth_null;
    sec->authKeyBits = 0;
    sec->signatureScheme = ssl_sig_none;
    sec->keaType = ssl_kea_null;
    sec->keaKeyBits = 0;
    sec->keaGroup = NULL;
}

SECStatus
ssl3_InitState(sslSocket *ss)
{
    SECStatus rv;

    ss->ssl3.policy = SSL_ALLOWED;

    ssl_InitSecState(&ss->sec);

    ssl_GetSpecWriteLock(ss);
    PR_INIT_CLIST(&ss->ssl3.hs.cipherSpecs);
    rv = ssl_SetupNullCipherSpec(ss, ssl_secret_read);
    rv |= ssl_SetupNullCipherSpec(ss, ssl_secret_write);
    ss->ssl3.pwSpec = ss->ssl3.prSpec = NULL;
    ssl_ReleaseSpecWriteLock(ss);
    if (rv != SECSuccess) {
        /* Rely on ssl_CreateNullCipherSpec() to set error code. */
        return SECFailure;
    }

    ss->ssl3.hs.sendingSCSV = PR_FALSE;
    ss->ssl3.hs.preliminaryInfo = 0;
    ss->ssl3.hs.ws = (ss->sec.isServer) ? wait_client_hello : idle_handshake;

    ssl3_ResetExtensionData(&ss->xtnData, ss);
    PR_INIT_CLIST(&ss->ssl3.hs.remoteExtensions);
    PR_INIT_CLIST(&ss->ssl3.hs.echOuterExtensions);
    if (IS_DTLS(ss)) {
        ss->ssl3.hs.sendMessageSeq = 0;
        ss->ssl3.hs.recvMessageSeq = 0;
        ss->ssl3.hs.rtTimer->timeout = DTLS_RETRANSMIT_INITIAL_MS;
        ss->ssl3.hs.rtRetries = 0;
        ss->ssl3.hs.recvdHighWater = -1;
        PR_INIT_CLIST(&ss->ssl3.hs.lastMessageFlight);
        dtls_SetMTU(ss, 0); /* Set the MTU to the highest plateau */
    }

    ss->ssl3.hs.currentSecret = NULL;
    ss->ssl3.hs.resumptionMasterSecret = NULL;
    ss->ssl3.hs.dheSecret = NULL;
    ss->ssl3.hs.clientEarlyTrafficSecret = NULL;
    ss->ssl3.hs.clientHsTrafficSecret = NULL;
    ss->ssl3.hs.serverHsTrafficSecret = NULL;
    ss->ssl3.hs.clientTrafficSecret = NULL;
    ss->ssl3.hs.serverTrafficSecret = NULL;
    ss->ssl3.hs.echHpkeCtx = NULL;
    ss->ssl3.hs.greaseEchSize = 100;
    ss->ssl3.hs.echAccepted = PR_FALSE;
    ss->ssl3.hs.echDecided = PR_FALSE;

    ss->ssl3.hs.clientAuthSignatureSchemes = NULL;
    ss->ssl3.hs.clientAuthSignatureSchemesLen = 0;

    PORT_Assert(!ss->ssl3.hs.messages.buf && !ss->ssl3.hs.messages.space);
    ss->ssl3.hs.messages.buf = NULL;
    ss->ssl3.hs.messages.space = 0;

    ss->ssl3.hs.receivedNewSessionTicket = PR_FALSE;
    PORT_Memset(&ss->ssl3.hs.newSessionTicket, 0,
                sizeof(ss->ssl3.hs.newSessionTicket));

    ss->ssl3.hs.zeroRttState = ssl_0rtt_none;
    return SECSuccess;
}

/* record the export policy for this cipher suite */
SECStatus
ssl3_SetPolicy(ssl3CipherSuite which, int policy)
{
    ssl3CipherSuiteCfg *suite;

    suite = ssl_LookupCipherSuiteCfgMutable(which, cipherSuites);
    if (suite == NULL) {
        return SECFailure; /* err code was set by ssl_LookupCipherSuiteCfg */
    }
    suite->policy = policy;

    return SECSuccess;
}

SECStatus
ssl3_GetPolicy(ssl3CipherSuite which, PRInt32 *oPolicy)
{
    const ssl3CipherSuiteCfg *suite;
    PRInt32 policy;
    SECStatus rv;

    suite = ssl_LookupCipherSuiteCfg(which, cipherSuites);
    if (suite) {
        policy = suite->policy;
        rv = SECSuccess;
    } else {
        policy = SSL_NOT_ALLOWED;
        rv = SECFailure; /* err code was set by Lookup. */
    }
    *oPolicy = policy;
    return rv;
}

/* record the user preference for this suite */
SECStatus
ssl3_CipherPrefSetDefault(ssl3CipherSuite which, PRBool enabled)
{
    ssl3CipherSuiteCfg *suite;

    suite = ssl_LookupCipherSuiteCfgMutable(which, cipherSuites);
    if (suite == NULL) {
        return SECFailure; /* err code was set by ssl_LookupCipherSuiteCfg */
    }
    suite->enabled = enabled;
    return SECSuccess;
}

/* return the user preference for this suite */
SECStatus
ssl3_CipherPrefGetDefault(ssl3CipherSuite which, PRBool *enabled)
{
    const ssl3CipherSuiteCfg *suite;
    PRBool pref;
    SECStatus rv;

    suite = ssl_LookupCipherSuiteCfg(which, cipherSuites);
    if (suite) {
        pref = suite->enabled;
        rv = SECSuccess;
    } else {
        pref = SSL_NOT_ALLOWED;
        rv = SECFailure; /* err code was set by Lookup. */
    }
    *enabled = pref;
    return rv;
}

SECStatus
ssl3_CipherPrefSet(sslSocket *ss, ssl3CipherSuite which, PRBool enabled)
{
    ssl3CipherSuiteCfg *suite;

    suite = ssl_LookupCipherSuiteCfgMutable(which, ss->cipherSuites);
    if (suite == NULL) {
        return SECFailure; /* err code was set by ssl_LookupCipherSuiteCfg */
    }
    suite->enabled = enabled;
    return SECSuccess;
}

SECStatus
ssl3_CipherPrefGet(const sslSocket *ss, ssl3CipherSuite which, PRBool *enabled)
{
    const ssl3CipherSuiteCfg *suite;
    PRBool pref;
    SECStatus rv;

    suite = ssl_LookupCipherSuiteCfg(which, ss->cipherSuites);
    if (suite) {
        pref = suite->enabled;
        rv = SECSuccess;
    } else {
        pref = SSL_NOT_ALLOWED;
        rv = SECFailure; /* err code was set by Lookup. */
    }
    *enabled = pref;
    return rv;
}

SECStatus
SSL_SignatureSchemePrefSet(PRFileDesc *fd, const SSLSignatureScheme *schemes,
                           unsigned int count)
{
    sslSocket *ss;
    unsigned int i;
    unsigned int supported = 0;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SignatureSchemePrefSet",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!count) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    for (i = 0; i < count; ++i) {
        if (ssl_IsSupportedSignatureScheme(schemes[i])) {
            ++supported;
        }
    }
    /* We don't check for duplicates, so it's possible to get too many. */
    if (supported > MAX_SIGNATURE_SCHEMES) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    ss->ssl3.signatureSchemeCount = 0;
    for (i = 0; i < count; ++i) {
        if (!ssl_IsSupportedSignatureScheme(schemes[i])) {
            SSL_DBG(("%d: SSL[%d]: invalid signature scheme %d ignored",
                     SSL_GETPID(), fd, schemes[i]));
            continue;
        }

        ss->ssl3.signatureSchemes[ss->ssl3.signatureSchemeCount++] = schemes[i];
    }

    if (ss->ssl3.signatureSchemeCount == 0) {
        PORT_SetError(SSL_ERROR_NO_SUPPORTED_SIGNATURE_ALGORITHM);
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
SSL_SignaturePrefSet(PRFileDesc *fd, const SSLSignatureAndHashAlg *algorithms,
                     unsigned int count)
{
    SSLSignatureScheme schemes[MAX_SIGNATURE_SCHEMES];
    unsigned int i;

    count = PR_MIN(PR_ARRAY_SIZE(schemes), count);
    for (i = 0; i < count; ++i) {
        schemes[i] = (algorithms[i].hashAlg << 8) | algorithms[i].sigAlg;
    }
    return SSL_SignatureSchemePrefSet(fd, schemes, count);
}

SECStatus
SSL_SignatureSchemePrefGet(PRFileDesc *fd, SSLSignatureScheme *schemes,
                           unsigned int *count, unsigned int maxCount)
{
    sslSocket *ss;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SignatureSchemePrefGet",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!schemes || !count ||
        maxCount < ss->ssl3.signatureSchemeCount) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    PORT_Memcpy(schemes, ss->ssl3.signatureSchemes,
                ss->ssl3.signatureSchemeCount * sizeof(SSLSignatureScheme));
    *count = ss->ssl3.signatureSchemeCount;
    return SECSuccess;
}

SECStatus
SSL_SignaturePrefGet(PRFileDesc *fd, SSLSignatureAndHashAlg *algorithms,
                     unsigned int *count, unsigned int maxCount)
{
    sslSocket *ss;
    unsigned int i;

    ss = ssl_FindSocket(fd);
    if (!ss) {
        SSL_DBG(("%d: SSL[%d]: bad socket in SSL_SignaturePrefGet",
                 SSL_GETPID(), fd));
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    if (!algorithms || !count ||
        maxCount < ss->ssl3.signatureSchemeCount) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    for (i = 0; i < ss->ssl3.signatureSchemeCount; ++i) {
        algorithms[i].hashAlg = (ss->ssl3.signatureSchemes[i] >> 8) & 0xff;
        algorithms[i].sigAlg = ss->ssl3.signatureSchemes[i] & 0xff;
    }
    *count = ss->ssl3.signatureSchemeCount;
    return SECSuccess;
}

unsigned int
SSL_SignatureMaxCount(void)
{
    return MAX_SIGNATURE_SCHEMES;
}

/* copy global default policy into socket. */
void
ssl3_InitSocketPolicy(sslSocket *ss)
{
    PORT_Memcpy(ss->cipherSuites, cipherSuites, sizeof(cipherSuites));
    PORT_Memcpy(ss->ssl3.signatureSchemes, defaultSignatureSchemes,
                sizeof(defaultSignatureSchemes));
    ss->ssl3.signatureSchemeCount = PR_ARRAY_SIZE(defaultSignatureSchemes);
}

/*
** If ssl3 socket has completed the first handshake, and is in idle state,
** then start a new handshake.
** If flushCache is true, the SID cache will be flushed first, forcing a
** "Full" handshake (not a session restart handshake), to be done.
**
** called from SSL_RedoHandshake(), which already holds the handshake locks.
*/
SECStatus
ssl3_RedoHandshake(sslSocket *ss, PRBool flushCache)
{
    sslSessionID *sid = ss->sec.ci.sid;
    SECStatus rv;

    PORT_Assert(ss->opt.noLocks || ssl_HaveSSL3HandshakeLock(ss));

    if (!ss->firstHsDone || (ss->ssl3.hs.ws != idle_handshake)) {
        PORT_SetError(SSL_ERROR_HANDSHAKE_NOT_COMPLETED);
        return SECFailure;
    }

    if (IS_DTLS(ss)) {
        dtls_RehandshakeCleanup(ss);
    }

    if (ss->opt.enableRenegotiation == SSL_RENEGOTIATE_NEVER ||
        ss->version >= SSL_LIBRARY_VERSION_TLS_1_3) {
        PORT_SetError(SSL_ERROR_RENEGOTIATION_NOT_ALLOWED);
        return SECFailure;
    }
    if (ss->version > ss->vrange.max || ss->version < ss->vrange.min) {
        PORT_SetError(SSL_ERROR_UNSUPPORTED_VERSION);
        return SECFailure;
    }

    if (sid && flushCache) {
        ssl_UncacheSessionID(ss); /* remove it from whichever cache it's in. */
        ssl_FreeSID(sid);         /* dec ref count and free if zero. */
        ss->sec.ci.sid = NULL;
    }

    ssl_GetXmitBufLock(ss); /**************************************/

    /* start off a new handshake. */
    if (ss->sec.isServer) {
        rv = ssl3_SendHelloRequest(ss);
    } else {
        rv = ssl3_SendClientHello(ss, client_hello_renegotiation);
    }

    ssl_ReleaseXmitBufLock(ss); /**************************************/
    return rv;
}

/* Called from ssl_DestroySocketContents() in sslsock.c */
void
ssl3_DestroySSL3Info(sslSocket *ss)
{

    if (ss->ssl3.clientCertificate != NULL)
        CERT_DestroyCertificate(ss->ssl3.clientCertificate);

    if (ss->ssl3.clientPrivateKey != NULL)
        SECKEY_DestroyPrivateKey(ss->ssl3.clientPrivateKey);

    if (ss->ssl3.hs.clientAuthSignatureSchemes != NULL) {
        PORT_Free(ss->ssl3.hs.clientAuthSignatureSchemes);
        ss->ssl3.hs.clientAuthSignatureSchemes = NULL;
        ss->ssl3.hs.clientAuthSignatureSchemesLen = 0;
    }

    if (ss->ssl3.peerCertArena != NULL)
        ssl3_CleanupPeerCerts(ss);

    if (ss->ssl3.clientCertChain != NULL) {
        CERT_DestroyCertificateList(ss->ssl3.clientCertChain);
        ss->ssl3.clientCertChain = NULL;
    }
    if (ss->ssl3.ca_list) {
        CERT_FreeDistNames(ss->ssl3.ca_list);
    }

    /* clean up handshake */
    if (ss->ssl3.hs.md5) {
        PK11_DestroyContext(ss->ssl3.hs.md5, PR_TRUE);
    }
    if (ss->ssl3.hs.sha) {
        PK11_DestroyContext(ss->ssl3.hs.sha, PR_TRUE);
    }
    if (ss->ssl3.hs.shaEchInner) {
        PK11_DestroyContext(ss->ssl3.hs.shaEchInner, PR_TRUE);
    }
    if (ss->ssl3.hs.shaPostHandshake) {
        PK11_DestroyContext(ss->ssl3.hs.shaPostHandshake, PR_TRUE);
    }
    if (ss->ssl3.hs.messages.buf) {
        sslBuffer_Clear(&ss->ssl3.hs.messages);
    }
    if (ss->ssl3.hs.echInnerMessages.buf) {
        sslBuffer_Clear(&ss->ssl3.hs.echInnerMessages);
    }
    if (ss->ssl3.hs.dtls13ClientMessageBuffer.buf) {
        sslBuffer_Clear(&ss->ssl3.hs.dtls13ClientMessageBuffer);
    }

    /* free the SSL3Buffer (msg_body) */
    PORT_Free(ss->ssl3.hs.msg_body.buf);

    SECITEM_FreeItem(&ss->ssl3.hs.newSessionTicket.ticket, PR_FALSE);
    SECITEM_FreeItem(&ss->ssl3.hs.srvVirtName, PR_FALSE);
    SECITEM_FreeItem(&ss->ssl3.hs.fakeSid, PR_FALSE);

    /* Destroy the DTLS data */
    if (IS_DTLS(ss)) {
        dtls_FreeHandshakeMessages(&ss->ssl3.hs.lastMessageFlight);
        if (ss->ssl3.hs.recvdFragments.buf) {
            PORT_Free(ss->ssl3.hs.recvdFragments.buf);
        }
    }

    /* Destroy remote extensions */
    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.remoteExtensions);
    ssl3_DestroyRemoteExtensions(&ss->ssl3.hs.echOuterExtensions);
    ssl3_DestroyExtensionData(&ss->xtnData);

    /* Destroy cipher specs */
    ssl_DestroyCipherSpecs(&ss->ssl3.hs.cipherSpecs);

    /* Destroy TLS 1.3 keys */
    if (ss->ssl3.hs.currentSecret)
        PK11_FreeSymKey(ss->ssl3.hs.currentSecret);
    if (ss->ssl3.hs.resumptionMasterSecret)
        PK11_FreeSymKey(ss->ssl3.hs.resumptionMasterSecret);
    if (ss->ssl3.hs.dheSecret)
        PK11_FreeSymKey(ss->ssl3.hs.dheSecret);
    if (ss->ssl3.hs.clientEarlyTrafficSecret)
        PK11_FreeSymKey(ss->ssl3.hs.clientEarlyTrafficSecret);
    if (ss->ssl3.hs.clientHsTrafficSecret)
        PK11_FreeSymKey(ss->ssl3.hs.clientHsTrafficSecret);
    if (ss->ssl3.hs.serverHsTrafficSecret)
        PK11_FreeSymKey(ss->ssl3.hs.serverHsTrafficSecret);
    if (ss->ssl3.hs.clientTrafficSecret)
        PK11_FreeSymKey(ss->ssl3.hs.clientTrafficSecret);
    if (ss->ssl3.hs.serverTrafficSecret)
        PK11_FreeSymKey(ss->ssl3.hs.serverTrafficSecret);
    if (ss->ssl3.hs.earlyExporterSecret)
        PK11_FreeSymKey(ss->ssl3.hs.earlyExporterSecret);
    if (ss->ssl3.hs.exporterSecret)
        PK11_FreeSymKey(ss->ssl3.hs.exporterSecret);

    ss->ssl3.hs.zeroRttState = ssl_0rtt_none;
    /* Destroy TLS 1.3 buffered early data. */
    tls13_DestroyEarlyData(&ss->ssl3.hs.bufferedEarlyData);

    /* Destroy TLS 1.3 PSKs. */
    tls13_DestroyPskList(&ss->ssl3.hs.psks);

    /* TLS 1.3 ECH state. */
    PK11_HPKE_DestroyContext(ss->ssl3.hs.echHpkeCtx, PR_TRUE);
    PORT_Free((void *)ss->ssl3.hs.echPublicName); /* CONST */
    sslBuffer_Clear(&ss->ssl3.hs.greaseEchBuf);

    /* TLS 1.3 GREASE (client) state. */
    tls13_ClientGreaseDestroy(ss);

    /* TLS ClientHello Extension Permutation state. */
    tls_ClientHelloExtensionPermutationDestroy(ss);
}

/* check if the current cipher spec is FIPS. We only need to
 * check the contexts here, if the kea, prf or keys were not FIPS,
 * that status would have been rolled up in the create context
 * call */
static PRBool
ssl_cipherSpecIsFips(ssl3CipherSpec *spec)
{
    if (!spec || !spec->cipherDef) {
        return PR_FALSE;
    }

    if (spec->cipherDef->type != type_aead) {
        if (spec->keyMaterial.macContext == NULL) {
            return PR_FALSE;
        }
        if (!PK11_ContextGetFIPSStatus(spec->keyMaterial.macContext)) {
            return PR_FALSE;
        }
    }
    if (!spec->cipherContext) {
        return PR_FALSE;
    }
    return PK11_ContextGetFIPSStatus(spec->cipherContext);
}

/* return true if the current operation is running in FIPS mode */
PRBool
ssl_isFIPS(sslSocket *ss)
{
    if (!ssl_cipherSpecIsFips(ss->ssl3.crSpec)) {
        return PR_FALSE;
    }
    return ssl_cipherSpecIsFips(ss->ssl3.cwSpec);
}

/*
 * parse the policy value for a single algorithm in a cipher_suite,
 *   return TRUE if we disallow by the cipher suite by policy
 *   (we don't have to parse any more algorithm policies on this cipher suite),
 *  otherwise return FALSE.
 *   1. If we don't have the required policy, disable by default, disallow by
 *      policy and return TRUE (no more processing needed).
 *   2. If we have the required policy, and we are disabled, return FALSE,
 *      (if we are disabled, we only need to parse policy, not default).
 *   3. If we have the required policy, and we aren't adjusting the defaults
 *      return FALSE. (only parsing the policy, not default).
 *   4. We have the required policy and we are adjusting the defaults.
 *      If we are setting default = FALSE, set isDisabled to true so that
 *      we don't try to re-enable the cipher suite based on a different
 *      algorithm.
 */
PRBool
ssl_HandlePolicy(int cipher_suite, SECOidTag policyOid,
                 PRUint32 requiredPolicy, PRBool *isDisabled)
{
    PRUint32 policy;
    SECStatus rv;

    /* first fetch the policy for this algorithm */
    rv = NSS_GetAlgorithmPolicy(policyOid, &policy);
    if (rv != SECSuccess) {
        return PR_FALSE; /* no policy value, continue to the next algorithm */
    }
    /* first, are we allowed by policy, if not turn off allow and disable */
    if (!(policy & requiredPolicy)) {
        ssl_CipherPrefSetDefault(cipher_suite, PR_FALSE);
        ssl_CipherPolicySet(cipher_suite, SSL_NOT_ALLOWED);
        return PR_TRUE;
    }
    /* If we are already disabled, or the policy isn't setting a default
     * we are done processing this algorithm */
    if (*isDisabled || (policy & NSS_USE_DEFAULT_NOT_VALID)) {
        return PR_FALSE;
    }
    /* set the default value for the cipher suite. If we disable the cipher
     * suite, remember that so we don't process the next default. This has
     * the effect of disabling the whole cipher suite if any of the
     * algorithms it uses are disabled by default. We still have to
     * process the upper level because the cipher suite is still allowed
     * by policy, and we may still have to disallow it based on other
     * algorithms in the cipher suite. */
    if (policy & NSS_USE_DEFAULT_SSL_ENABLE) {
        ssl_CipherPrefSetDefault(cipher_suite, PR_TRUE);
    } else {
        *isDisabled = PR_TRUE;
        ssl_CipherPrefSetDefault(cipher_suite, PR_FALSE);
    }
    return PR_FALSE;
}

#define MAP_NULL(x) (((x) != 0) ? (x) : SEC_OID_NULL_CIPHER)

SECStatus
ssl3_ApplyNSSPolicy(void)
{
    unsigned i;
    SECStatus rv;
    PRUint32 policy = 0;

    rv = NSS_GetAlgorithmPolicy(SEC_OID_APPLY_SSL_POLICY, &policy);
    if (rv != SECSuccess || !(policy & NSS_USE_POLICY_IN_SSL)) {
        return SECSuccess; /* do nothing */
    }

    /* disable every ciphersuite */
    for (i = 1; i < PR_ARRAY_SIZE(cipher_suite_defs); ++i) {
        const ssl3CipherSuiteDef *suite = &cipher_suite_defs[i];
        SECOidTag policyOid;
        PRBool isDisabled = PR_FALSE;

        /* if we haven't explicitly disabled it below enable by policy */
        ssl_CipherPolicySet(suite->cipher_suite, SSL_ALLOWED);

        /* now check the various key exchange, ciphers and macs and
         * if we ever disallow by policy, we are done, go to the next cipher
         */
        policyOid = MAP_NULL(kea_defs[suite->key_exchange_alg].oid);
        if (ssl_HandlePolicy(suite->cipher_suite, policyOid,
                             NSS_USE_ALG_IN_SSL_KX, &isDisabled)) {
            continue;
        }

        policyOid = MAP_NULL(ssl_GetBulkCipherDef(suite)->oid);
        if (ssl_HandlePolicy(suite->cipher_suite, policyOid,
                             NSS_USE_ALG_IN_SSL, &isDisabled)) {
            continue;
        }

        if (ssl_GetBulkCipherDef(suite)->type != type_aead) {
            policyOid = MAP_NULL(ssl_GetMacDefByAlg(suite->mac_alg)->oid);
            if (ssl_HandlePolicy(suite->cipher_suite, policyOid,
                                 NSS_USE_ALG_IN_SSL, &isDisabled)) {
                continue;
            }
        }
    }

    rv = ssl3_ConstrainRangeByPolicy();

    return rv;
}

/* End of ssl3con.c */
