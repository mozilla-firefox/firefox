/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * The following handles the loading, unloading and management of
 * various PCKS #11 modules
 */

#include <ctype.h>
#include <assert.h>
#include "pkcs11.h"
#include "seccomon.h"
#include "secmod.h"
#include "secmodi.h"
#include "secmodti.h"
#include "pki3hack.h"
#include "secerr.h"
#include "nss.h"
#include "utilpars.h"
#include "pk11pub.h"

/* create a new module */
static SECMODModule *
secmod_NewModule(void)
{
    SECMODModule *newMod;
    PLArenaPool *arena;

    /* create an arena in which dllName and commonName can be
     * allocated.
     */
    arena = PORT_NewArena(512);
    if (arena == NULL) {
        return NULL;
    }

    newMod = (SECMODModule *)PORT_ArenaAlloc(arena, sizeof(SECMODModule));
    if (newMod == NULL) {
        PORT_FreeArena(arena, PR_FALSE);
        return NULL;
    }

    /*
     * initialize of the fields of the module
     */
    newMod->arena = arena;
    newMod->internal = PR_FALSE;
    newMod->loaded = PR_FALSE;
    newMod->isFIPS = PR_FALSE;
    newMod->dllName = NULL;
    newMod->commonName = NULL;
    newMod->library = NULL;
    newMod->functionList = NULL;
    newMod->slotCount = 0;
    newMod->slots = NULL;
    newMod->slotInfo = NULL;
    newMod->slotInfoCount = 0;
    newMod->refCount = 1;
    newMod->ssl[0] = 0;
    newMod->ssl[1] = 0;
    newMod->libraryParams = NULL;
    newMod->moduleDBFunc = NULL;
    newMod->parent = NULL;
    newMod->isCritical = PR_FALSE;
    newMod->isModuleDB = PR_FALSE;
    newMod->moduleDBOnly = PR_FALSE;
    newMod->trustOrder = 0;
    newMod->cipherOrder = 0;
    newMod->evControlMask = 0;
    newMod->refLock = PR_NewLock();
    if (newMod->refLock == NULL) {
        PORT_FreeArena(arena, PR_FALSE);
        return NULL;
    }
    return newMod;
}

/* private flags for isModuleDB (field in SECMODModule). */
/* The meaing of these flags is as follows:
 *
 * SECMOD_FLAG_MODULE_DB_IS_MODULE_DB - This is a module that accesses the
 *   database of other modules to load. Module DBs are loadable modules that
 *   tells NSS which PKCS #11 modules to load and when. These module DBs are
 *   chainable. That is, one module DB can load another one. NSS system init
 *   design takes advantage of this feature. In system NSS, a fixed system
 *   module DB loads the system defined libraries, then chains out to the
 *   traditional module DBs to load any system or user configured modules
 *   (like smart cards). This bit is the same as the already existing meaning
 *   of  isModuleDB = PR_TRUE. None of the other module db flags should be set
 *   if this flag isn't on.
 *
 * SECMOD_FLAG_MODULE_DB_SKIP_FIRST - This flag tells NSS to skip the first
 *   PKCS #11 module presented by a module DB. This allows the OS to load a
 *   softoken from the system module, then ask the existing module DB code to
 *   load the other PKCS #11 modules in that module DB (skipping it's request
 *   to load softoken). This gives the system init finer control over the
 *   configuration of that softoken module.
 *
 * SECMOD_FLAG_MODULE_DB_DEFAULT_MODDB - This flag allows system init to mark a
 *   different module DB as the 'default' module DB (the one in which
 *   'Add module' changes will go). Without this flag NSS takes the first
 *   module as the default Module DB, but in system NSS, that first module
 *   is the system module, which is likely read only (at least to the user).
 *   This  allows system NSS to delegate those changes to the user's module DB,
 *   preserving the user's ability to load new PKCS #11 modules (which only
 *   affect him), from existing applications like Firefox.
 */
#define SECMOD_FLAG_MODULE_DB_IS_MODULE_DB 0x01 /* must be set if any of the \
                                                 *other flags are set */
#define SECMOD_FLAG_MODULE_DB_SKIP_FIRST 0x02
#define SECMOD_FLAG_MODULE_DB_DEFAULT_MODDB 0x04
#define SECMOD_FLAG_MODULE_DB_POLICY_ONLY 0x08

/* private flags for internal (field in SECMODModule). */
/* The meaing of these flags is as follows:
 *
 * SECMOD_FLAG_INTERNAL_IS_INTERNAL - This is a marks the the module is
 *   the internal module (that is, softoken). This bit is the same as the
 *   already existing meaning of internal = PR_TRUE. None of the other
 *   internal flags should be set if this flag isn't on.
 *
 * SECMOD_FLAG_MODULE_INTERNAL_KEY_SLOT - This flag allows system init to mark
 *   a  different slot returned byt PK11_GetInternalKeySlot(). The 'primary'
 *   slot defined by this module will be the new internal key slot.
 */
#define SECMOD_FLAG_INTERNAL_IS_INTERNAL 0x01 /* must be set if any of \
                                               *the other flags are set */
#define SECMOD_FLAG_INTERNAL_KEY_SLOT 0x02

/* private flags for policy check. */
#define SECMOD_FLAG_POLICY_CHECK_IDENTIFIER 0x01
#define SECMOD_FLAG_POLICY_CHECK_VALUE 0x02

/*
 * for 3.4 we continue to use the old SECMODModule structure
 */
SECMODModule *
SECMOD_CreateModule(const char *library, const char *moduleName,
                    const char *parameters, const char *nss)
{
    return SECMOD_CreateModuleEx(library, moduleName, parameters, nss, NULL);
}

/*
 * NSS config options format:
 *
 * The specified ciphers will be allowed by policy, but an application
 * may allow more by policy explicitly:
 * config="allow=curve1:curve2:hash1:hash2:rsa-1024..."
 *
 * Only the specified hashes and curves will be allowed:
 * config="disallow=all allow=sha1:sha256:secp256r1:secp384r1"
 *
 * Only the specified hashes and curves will be allowed, and
 *  RSA keys of 2048 or more will be accepted, and DH key exchange
 *  with 1024-bit primes or more:
 * config="disallow=all allow=sha1:sha256:secp256r1:secp384r1:min-rsa=2048:min-dh=1024"
 *
 * A policy that enables the AES ciphersuites and the SECP256/384 curves:
 * config="allow=aes128-cbc:aes128-gcm:TLS1.0:TLS1.2:TLS1.1:HMAC-SHA1:SHA1:SHA256:SHA384:RSA:ECDHE-RSA:SECP256R1:SECP384R1"
 *
 * Disallow values are parsed first, then allow values, independent of the
 * order they appear.
 *
 * flags: turn on the following flags:
 *    policy-lock: turn off the ability for applications to change policy with
 *                 the call NSS_SetAlgorithmPolicy or the other system policy
 *                 calls (SSL_SetPolicy, etc.)
 *    ssl-lock:    turn off the ability to change the ssl defaults.
 *
 * The following only apply to ssl cipher suites (future smime)
 *
 * enable: turn on ciphersuites by default.
 * disable: turn off ciphersuites by default without disallowing them by policy.
 *
 *
 */

typedef struct {
    const char *name;
    unsigned name_size;
    SECOidTag oid;
    PRUint32 val;
} oidValDef;

typedef struct {
    const char *name;
    unsigned name_size;
    PRInt32 option;
} optionFreeDef;

typedef struct {
    const char *name;
    unsigned name_size;
    PRUint32 flag;
} policyFlagDef;

/*
 *  This table should be merged with the SECOID table.
 */
#define CIPHER_NAME(x) x, (sizeof(x) - 1)
static const oidValDef curveOptList[] = {
    /* Curves */
    { CIPHER_NAME("PRIME192V1"), SEC_OID_ANSIX962_EC_PRIME192V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("PRIME192V2"), SEC_OID_ANSIX962_EC_PRIME192V2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("PRIME192V3"), SEC_OID_ANSIX962_EC_PRIME192V3,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("PRIME239V1"), SEC_OID_ANSIX962_EC_PRIME239V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("PRIME239V2"), SEC_OID_ANSIX962_EC_PRIME239V2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("PRIME239V3"), SEC_OID_ANSIX962_EC_PRIME239V3,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("PRIME256V1"), SEC_OID_ANSIX962_EC_PRIME256V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP112R1"), SEC_OID_SECG_EC_SECP112R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP112R2"), SEC_OID_SECG_EC_SECP112R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP128R1"), SEC_OID_SECG_EC_SECP128R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP128R2"), SEC_OID_SECG_EC_SECP128R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP160K1"), SEC_OID_SECG_EC_SECP160K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP160R1"), SEC_OID_SECG_EC_SECP160R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP160R2"), SEC_OID_SECG_EC_SECP160R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP192K1"), SEC_OID_SECG_EC_SECP192K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP192R1"), SEC_OID_ANSIX962_EC_PRIME192V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP224K1"), SEC_OID_SECG_EC_SECP224K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP256K1"), SEC_OID_SECG_EC_SECP256K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP256R1"), SEC_OID_ANSIX962_EC_PRIME256V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP384R1"), SEC_OID_SECG_EC_SECP384R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECP521R1"), SEC_OID_SECG_EC_SECP521R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("CURVE25519"), SEC_OID_CURVE25519,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    /* NOTE, don't use '0' to indicate default off. Setting '0'
     * makes this entry unmanagable by the policy code (including
     * turning the entry off. If you want an entry off by default
     * simply explictly flip the bits in SECOID_Init()
     * (util/secoid.c) */
    { CIPHER_NAME("XYBER768D00"), SEC_OID_XYBER768D00,
      NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("X25519MLKEM768"), SEC_OID_MLKEM768X25519,
      NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("SECP256R1MLKEM768"), SEC_OID_SECP256R1MLKEM768,
      NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("SECP384R1MLKEM1024"), SEC_OID_SECP384R1MLKEM1024,
      NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("MLKEM768X25519"), SEC_OID_MLKEM768X25519,
      NSS_USE_ALG_IN_SSL_KX },
    /* ANSI X9.62 named elliptic curves (characteristic two field) */
    { CIPHER_NAME("C2PNB163V1"), SEC_OID_ANSIX962_EC_C2PNB163V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB163V2"), SEC_OID_ANSIX962_EC_C2PNB163V2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB163V3"), SEC_OID_ANSIX962_EC_C2PNB163V3,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB176V1"), SEC_OID_ANSIX962_EC_C2PNB176V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB191V1"), SEC_OID_ANSIX962_EC_C2TNB191V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB191V2"), SEC_OID_ANSIX962_EC_C2TNB191V2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB191V3"), SEC_OID_ANSIX962_EC_C2TNB191V3,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2ONB191V4"), SEC_OID_ANSIX962_EC_C2ONB191V4,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2ONB191V5"), SEC_OID_ANSIX962_EC_C2ONB191V5,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB208W1"), SEC_OID_ANSIX962_EC_C2PNB208W1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB239V1"), SEC_OID_ANSIX962_EC_C2TNB239V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB239V2"), SEC_OID_ANSIX962_EC_C2TNB239V2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB239V3"), SEC_OID_ANSIX962_EC_C2TNB239V3,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2ONB239V4"), SEC_OID_ANSIX962_EC_C2ONB239V4,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2ONB239V5"), SEC_OID_ANSIX962_EC_C2ONB239V5,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB272W1"), SEC_OID_ANSIX962_EC_C2PNB272W1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB304W1"), SEC_OID_ANSIX962_EC_C2PNB304W1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB359V1"), SEC_OID_ANSIX962_EC_C2TNB359V1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2PNB368W1"), SEC_OID_ANSIX962_EC_C2PNB368W1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("C2TNB431R1"), SEC_OID_ANSIX962_EC_C2TNB431R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    /* SECG named elliptic curves (characteristic two field) */
    { CIPHER_NAME("SECT113R1"), SEC_OID_SECG_EC_SECT113R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT131R1"), SEC_OID_SECG_EC_SECT113R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT131R1"), SEC_OID_SECG_EC_SECT131R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT131R2"), SEC_OID_SECG_EC_SECT131R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT163K1"), SEC_OID_SECG_EC_SECT163K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT163R1"), SEC_OID_SECG_EC_SECT163R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT163R2"), SEC_OID_SECG_EC_SECT163R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT193R1"), SEC_OID_SECG_EC_SECT193R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT193R2"), SEC_OID_SECG_EC_SECT193R2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT233K1"), SEC_OID_SECG_EC_SECT233K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT233R1"), SEC_OID_SECG_EC_SECT233R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT239K1"), SEC_OID_SECG_EC_SECT239K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT283K1"), SEC_OID_SECG_EC_SECT283K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT283R1"), SEC_OID_SECG_EC_SECT283R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT409K1"), SEC_OID_SECG_EC_SECT409K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT409R1"), SEC_OID_SECG_EC_SECT409R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT571K1"), SEC_OID_SECG_EC_SECT571K1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("SECT571R1"), SEC_OID_SECG_EC_SECT571R1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_CERT_SIGNATURE },
};

static const oidValDef hashOptList[] = {
    /* Hashes */
    { CIPHER_NAME("MD2"), SEC_OID_MD2,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("MD4"), SEC_OID_MD4,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("MD5"), SEC_OID_MD5,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA1"), SEC_OID_SHA1,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA224"), SEC_OID_SHA224,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA256"), SEC_OID_SHA256,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA384"), SEC_OID_SHA384,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA512"), SEC_OID_SHA512,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA3-224"), SEC_OID_SHA3_224,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA3-256"), SEC_OID_SHA3_256,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA3-384"), SEC_OID_SHA3_384,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("SHA3-512"), SEC_OID_SHA3_512,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SMIME |
          NSS_USE_ALG_IN_PKCS12 }
};

static const oidValDef macOptList[] = {
    /* MACs */
    { CIPHER_NAME("HMAC-MD5"), SEC_OID_HMAC_MD5,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA1"), SEC_OID_HMAC_SHA1,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA224"), SEC_OID_HMAC_SHA224,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA256"), SEC_OID_HMAC_SHA256,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA384"), SEC_OID_HMAC_SHA384,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA512"), SEC_OID_HMAC_SHA512,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA3-224"), SEC_OID_HMAC_SHA3_224,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA3-256"), SEC_OID_HMAC_SHA3_256,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA3-384"), SEC_OID_HMAC_SHA3_384,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("HMAC-SHA3-512"), SEC_OID_HMAC_SHA3_512,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
};

static const oidValDef cipherOptList[] = {
    /* Ciphers */
    { CIPHER_NAME("AES128-CBC"), SEC_OID_AES_128_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("AES192-CBC"), SEC_OID_AES_192_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("AES256-CBC"), SEC_OID_AES_256_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("AES128-GCM"), SEC_OID_AES_128_GCM, NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("AES192-GCM"), SEC_OID_AES_192_GCM, NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("AES256-GCM"), SEC_OID_AES_256_GCM, NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("CAMELLIA128-CBC"), SEC_OID_CAMELLIA_128_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("CAMELLIA192-CBC"), SEC_OID_CAMELLIA_192_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("CAMELLIA256-CBC"), SEC_OID_CAMELLIA_256_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("CHACHA20-POLY1305"), SEC_OID_CHACHA20_POLY1305, NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("SEED-CBC"), SEC_OID_SEED_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("DES-EDE3-CBC"), SEC_OID_DES_EDE3_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("DES-40-CBC"), SEC_OID_DES_40_CBC,
      NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SMIME | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("DES-CBC"), SEC_OID_DES_CBC, NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("NULL-CIPHER"), SEC_OID_NULL_CIPHER, NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("RC2"), SEC_OID_RC2_CBC, NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("RC2-40-CBC"), SEC_OID_RC2_40_CBC, NSS_USE_ALG_IN_SMIME },
    { CIPHER_NAME("RC2-64-CBC"), SEC_OID_RC2_64_CBC, NSS_USE_ALG_IN_SMIME },
    { CIPHER_NAME("RC2-128-CBC"), SEC_OID_RC2_128_CBC, NSS_USE_ALG_IN_SMIME },
    { CIPHER_NAME("RC4"), SEC_OID_RC4, NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_PKCS12 },
    { CIPHER_NAME("IDEA"), SEC_OID_IDEA_CBC, NSS_USE_ALG_IN_SSL },
};

static const oidValDef kxOptList[] = {
    /* Key exchange */
    { CIPHER_NAME("RSA"), SEC_OID_TLS_RSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("RSA-EXPORT"), SEC_OID_TLS_RSA_EXPORT, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("DHE-RSA"), SEC_OID_TLS_DHE_RSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("DHE-DSS"), SEC_OID_TLS_DHE_DSS, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("DH-RSA"), SEC_OID_TLS_DH_RSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("DH-DSS"), SEC_OID_TLS_DH_DSS, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("ECDHE-ECDSA"), SEC_OID_TLS_ECDHE_ECDSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("ECDHE-RSA"), SEC_OID_TLS_ECDHE_RSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("ECDH-ECDSA"), SEC_OID_TLS_ECDH_ECDSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("ECDH-RSA"), SEC_OID_TLS_ECDH_RSA, NSS_USE_ALG_IN_SSL_KX },
    { CIPHER_NAME("TLS-REQUIRE-EMS"), SEC_OID_TLS_REQUIRE_EMS, NSS_USE_ALG_IN_SSL_KX },

};

static const oidValDef smimeKxOptList[] = {
    /* Key exchange */
    { CIPHER_NAME("RSA-PKCS"), SEC_OID_PKCS1_RSA_ENCRYPTION, NSS_USE_ALG_IN_SMIME_KX },
    { CIPHER_NAME("RSA-OAEP"), SEC_OID_PKCS1_RSA_OAEP_ENCRYPTION, NSS_USE_ALG_IN_SMIME_KX },
    { CIPHER_NAME("ECDH"), SEC_OID_ECDH_KEA, NSS_USE_ALG_IN_SMIME_KX },
    { CIPHER_NAME("DH"), SEC_OID_X942_DIFFIE_HELMAN_KEY, NSS_USE_ALG_IN_SMIME_KX },
};

static const oidValDef signOptList[] = {
    /* Signatures */
    { CIPHER_NAME("DSA"), SEC_OID_ANSIX9_DSA_SIGNATURE,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("RSA-PKCS"), SEC_OID_PKCS1_RSA_ENCRYPTION,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("RSA-PSS"), SEC_OID_PKCS1_RSA_PSS_SIGNATURE,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("ECDSA"), SEC_OID_ANSIX962_EC_PUBLIC_KEY,
      NSS_USE_ALG_IN_SSL_KX | NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("ED25519"), SEC_OID_ED25519_PUBLIC_KEY,
      NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("ML-DSA-44"), SEC_OID_ML_DSA_44,
      NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("ML-DSA-65"), SEC_OID_ML_DSA_65,
      NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("ML-DSA-87"), SEC_OID_ML_DSA_87,
      NSS_USE_ALG_IN_SIGNATURE | NSS_USE_ALG_IN_SSL_KX },
};

typedef struct {
    const oidValDef *list;
    PRUint32 entries;
    const char *description;
    PRBool allowEmpty;
} algListsDef;

static const algListsDef algOptLists[] = {
    { curveOptList, PR_ARRAY_SIZE(curveOptList), "ECC", PR_FALSE },
    { hashOptList, PR_ARRAY_SIZE(hashOptList), "HASH", PR_FALSE },
    { macOptList, PR_ARRAY_SIZE(macOptList), "MAC", PR_FALSE },
    { cipherOptList, PR_ARRAY_SIZE(cipherOptList), "CIPHER", PR_FALSE },
    { kxOptList, PR_ARRAY_SIZE(kxOptList), "SSL-KX", PR_FALSE },
    { smimeKxOptList, PR_ARRAY_SIZE(smimeKxOptList), "SMIME-KX", PR_TRUE },
    { signOptList, PR_ARRAY_SIZE(signOptList), "OTHER-SIGN", PR_FALSE },
};

static const optionFreeDef sslOptList[] = {
    /* Versions */
    { CIPHER_NAME("SSL2.0"), 0x002 },
    { CIPHER_NAME("SSL3.0"), 0x300 },
    { CIPHER_NAME("SSL3.1"), 0x301 },
    { CIPHER_NAME("TLS1.0"), 0x301 },
    { CIPHER_NAME("TLS1.1"), 0x302 },
    { CIPHER_NAME("TLS1.2"), 0x303 },
    { CIPHER_NAME("TLS1.3"), 0x304 },
    { CIPHER_NAME("DTLS1.0"), 0x302 },
    { CIPHER_NAME("DTLS1.1"), 0x302 },
    { CIPHER_NAME("DTLS1.2"), 0x303 },
    { CIPHER_NAME("DTLS1.3"), 0x304 },
};

static const optionFreeDef keySizeFlagsList[] = {
    { CIPHER_NAME("KEY-SIZE-SSL"), NSS_KEY_SIZE_POLICY_SSL_FLAG },
    { CIPHER_NAME("KEY-SIZE-SIGN"), NSS_KEY_SIZE_POLICY_SIGN_FLAG },
    { CIPHER_NAME("KEY-SIZE-VERIFY"), NSS_KEY_SIZE_POLICY_VERIFY_FLAG },
    { CIPHER_NAME("KEY-SIZE-SMIME"), NSS_KEY_SIZE_POLICY_SMIME_FLAG },
    { CIPHER_NAME("KEY-SIZE-ALL"), NSS_KEY_SIZE_POLICY_ALL_FLAGS },
};

static const optionFreeDef freeOptList[] = {

    /* Restrictions for asymetric keys */
    { CIPHER_NAME("RSA-MIN"), NSS_RSA_MIN_KEY_SIZE },
    { CIPHER_NAME("DH-MIN"), NSS_DH_MIN_KEY_SIZE },
    { CIPHER_NAME("DSA-MIN"), NSS_DSA_MIN_KEY_SIZE },
    { CIPHER_NAME("ECC-MIN"), NSS_ECC_MIN_KEY_SIZE },
    /* what operations doe the key size apply to */
    { CIPHER_NAME("KEY-SIZE-FLAGS"), NSS_KEY_SIZE_POLICY_FLAGS },
    /* constraints on SSL Protocols */
    { CIPHER_NAME("TLS-VERSION-MIN"), NSS_TLS_VERSION_MIN_POLICY },
    { CIPHER_NAME("TLS-VERSION-MAX"), NSS_TLS_VERSION_MAX_POLICY },
    /* constraints on DTLS Protocols */
    { CIPHER_NAME("DTLS-VERSION-MIN"), NSS_DTLS_VERSION_MIN_POLICY },
    { CIPHER_NAME("DTLS-VERSION-MAX"), NSS_DTLS_VERSION_MAX_POLICY }
};

static const policyFlagDef policyFlagList[] = {
    { CIPHER_NAME("SSL"), NSS_USE_ALG_IN_SSL },
    { CIPHER_NAME("SSL-KEY-EXCHANGE"), NSS_USE_ALG_IN_SSL_KX },
    /* add other key exhanges in the future */
    { CIPHER_NAME("KEY-EXCHANGE"), NSS_USE_ALG_IN_KEY_EXCHANGE },
    { CIPHER_NAME("CERT-SIGNATURE"), NSS_USE_ALG_IN_CERT_SIGNATURE },
    { CIPHER_NAME("CMS-SIGNATURE"), NSS_USE_ALG_IN_SMIME_SIGNATURE },
    { CIPHER_NAME("SMIME-SIGNATURE"), NSS_USE_ALG_IN_SMIME_SIGNATURE },
    { CIPHER_NAME("ALL-SIGNATURE"), NSS_USE_ALG_IN_SIGNATURE },
    { CIPHER_NAME("PKCS12"), NSS_USE_ALG_IN_PKCS12 },
    /* only use in allow */
    { CIPHER_NAME("PKCS12-LEGACY"), NSS_USE_ALG_IN_PKCS12_DECRYPT },
    /* only use in disallow */
    { CIPHER_NAME("PKCS12-ENCRYPT"), NSS_USE_ALG_IN_PKCS12_ENCRYPT },
    { CIPHER_NAME("SMIME"), NSS_USE_ALG_IN_SMIME },
    /* only use in allow, enable */
    { CIPHER_NAME("SMIME-LEGACY"), NSS_USE_ALG_IN_SMIME_LEGACY },
    /* only use in disallow, disable */
    { CIPHER_NAME("SMIME-ENCRYPT"), NSS_USE_ALG_IN_SMIME_ENCRYPT },
    { CIPHER_NAME("SMIME-KEY-EXCHANGE"), NSS_USE_ALG_IN_SMIME_KX },
    /* only use in allow */
    { CIPHER_NAME("SMIME-KEY-EXCHANGE-LEGACY"), NSS_USE_ALG_IN_SMIME_KX_LEGACY },
    /* only use in disallow */
    { CIPHER_NAME("SMIME-KEY-EXCHANGE-ENCRYPT"), NSS_USE_ALG_IN_SMIME_KX_ENCRYPT },
    /* sign turns off all signatures, but doesn't change the
     * allowance for specific signatures... for example:
     *     disallow=sha256/all allow=sha256/signature
     * doesn't allow cert-signatures or sime-signatures, where
     *     disallow=sha256/all allow=sha256/all-signature
     * does. however,
     *     disallow=sha256/signature
     * and
     *     disallow=sha256/all-signature
     * are equivalent in effect */
    { CIPHER_NAME("SIGNATURE"), NSS_USE_ALG_IN_ANY_SIGNATURE },
    /* enable/allow algorithms for legacy (read/verify)operations */
    { CIPHER_NAME("LEGACY"), NSS_USE_ALG_IN_PKCS12_DECRYPT |
                                 NSS_USE_ALG_IN_SMIME_LEGACY |
                                 NSS_USE_ALG_IN_SMIME_KX_LEGACY },
    /* enable/disable everything */
    { CIPHER_NAME("ALL"), NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SSL_KX |
                              NSS_USE_ALG_IN_PKCS12 | NSS_USE_ALG_IN_SMIME |
                              NSS_USE_ALG_IN_SIGNATURE |
                              NSS_USE_ALG_IN_SMIME_KX },
    { CIPHER_NAME("NONE"), 0 }
};

/*
 *  Get the next cipher on the list. point to the next one in 'next'.
 *  return the length;
 */
static const char *
secmod_ArgGetSubValue(const char *cipher, char sep1, char sep2,
                      int *len, const char **next)
{
    const char *start = cipher;

    if (start == NULL) {
        *len = 0;
        *next = NULL;
        return start;
    }

    for (; *cipher && *cipher != sep2; cipher++) {
        if (*cipher == sep1) {
            *next = cipher + 1;
            *len = cipher - start;
            return start;
        }
    }
    *next = NULL;
    *len = cipher - start;
    return start;
}

static PRUint32
secmod_parsePolicyValue(const char *policyFlags, int policyLength,
                        PRBool printPolicyFeedback, PRUint32 policyCheckFlags)
{
    const char *flag, *currentString;
    PRUint32 flags = 0;
    int i;

    for (currentString = policyFlags; currentString &&
                                      currentString < policyFlags + policyLength;) {
        int length;
        PRBool unknown = PR_TRUE;
        flag = secmod_ArgGetSubValue(currentString, ',', ':', &length,
                                     &currentString);
        if (length == 0) {
            continue;
        }
        for (i = 0; i < PR_ARRAY_SIZE(policyFlagList); i++) {
            const policyFlagDef *policy = &policyFlagList[i];
            unsigned name_size = policy->name_size;
            if ((policy->name_size == length) &&
                PORT_Strncasecmp(policy->name, flag, name_size) == 0) {
                flags |= policy->flag;
                unknown = PR_FALSE;
                break;
            }
        }
        if (unknown && printPolicyFeedback &&
            (policyCheckFlags & SECMOD_FLAG_POLICY_CHECK_VALUE)) {
            PR_SetEnv("NSS_POLICY_FAIL=1");
            fprintf(stderr, "NSS-POLICY-FAIL %.*s: unknown value: %.*s\n",
                    policyLength, policyFlags, length, flag);
        }
    }
    return flags;
}

/* allow symbolic names for values. The only ones currently defines or
 * SSL protocol versions. */
static SECStatus
secmod_getPolicyOptValue(const char *policyValue, int policyValueLength,
                         PRInt32 *result)
{
    PRInt32 val = atoi(policyValue);
    int i;

    if ((val != 0) || (*policyValue == '0')) {
        *result = val;
        return SECSuccess;
    }
    if (policyValueLength == 0) {
        return SECFailure;
    }
    /* handle any ssl strings */
    for (i = 0; i < PR_ARRAY_SIZE(sslOptList); i++) {
        if (policyValueLength == sslOptList[i].name_size &&
            PORT_Strncasecmp(sslOptList[i].name, policyValue,
                             sslOptList[i].name_size) == 0) {
            *result = sslOptList[i].option;
            return SECSuccess;
        }
    }
    /* handle key_size flags. Each flag represents a bit, which
     * gets or'd together. They can be separated by , | or + */
    val = 0;
    while (policyValueLength > 0) {
        PRBool found = PR_FALSE;
        for (i = 0; i < PR_ARRAY_SIZE(keySizeFlagsList); i++) {
            if (PORT_Strncasecmp(keySizeFlagsList[i].name, policyValue,
                                 keySizeFlagsList[i].name_size) == 0) {
                val |= keySizeFlagsList[i].option;
                found = PR_TRUE;
                policyValue += keySizeFlagsList[i].name_size;
                policyValueLength -= keySizeFlagsList[i].name_size;
                break;
            }
        }
        if (!found) {
            return SECFailure;
        }
        if (*policyValue == ',' || *policyValue == '|' || *policyValue == '+') {
            policyValue++;
            policyValueLength--;
        }
    }
    *result = val;
    return SECSuccess;
}

/* Policy operations:
 *     Disallow: operation is disallowed by policy. Implies disabled.
 *     Allow: operation is allowed by policy (but could be disabled).
 *     Disable: operation is turned off by default (but could be allowed).
 *     Enable: operation is enabled by default. Implies allowed.
 */
typedef enum {
    NSS_DISALLOW,
    NSS_ALLOW,
    NSS_DISABLE,
    NSS_ENABLE
} NSSPolicyOperation;

/* Enable/Disable only apply to SSL cipher suites and S/MIME symetric algorithms.
 * Enable/Disable is implemented by clearing the DEFAULT_NOT_VALID
 * flag, then setting the NSS_USE_DEFAULT_SSL_ENABLE and
 * NSS_USE_DEFAULT_SMIME_ENABLE flags to the correct value. The ssl
 * policy code will then sort out what to set based on ciphers and
 * cipher suite values and the smime policy code will sort
 * out which ciphers to include in capabilities based on these values */
static SECStatus
secmod_setDefault(SECOidTag oid, NSSPolicyOperation operation,
                  PRUint32 value)
{
    SECStatus rv = SECSuccess;
    PRUint32 policy;
    PRUint32 useDefault = 0;
    PRUint32 set = 0;
    /* we always clear the default not valid flag as this operation will
     * make the defaults valid */
    PRUint32 clear = NSS_USE_DEFAULT_NOT_VALID;

    /* what values are we trying to change */
    /* if either SSL or SSL_KX is set, enable SSL */
    if (value & (NSS_USE_ALG_IN_SSL | NSS_USE_ALG_IN_SSL_KX)) {
        useDefault |= NSS_USE_DEFAULT_SSL_ENABLE;
    }
    /* only bulk ciphers are configured as enable in S/MIME, only
     * enable them if both SMIME bits are set */
    if ((value & NSS_USE_ALG_IN_SMIME) == NSS_USE_ALG_IN_SMIME) {
        useDefault |= NSS_USE_DEFAULT_SMIME_ENABLE;
    }

    /* on disable we clear, on enable we set */
    if (operation == NSS_DISABLE) {
        clear |= useDefault;
    } else {
        /* we also turn the cipher on by policy if we enable it,
         * so include the policy bits */
        set |= value | useDefault;
    }

    /* if we haven't set the not valid flag yet, then we need to
     * clear any of the other bits we aren't actually setting as well.
     */
    rv = NSS_GetAlgorithmPolicy(oid, &policy);
    if (rv != SECSuccess) {
        return rv;
    }
    if (policy & NSS_USE_DEFAULT_NOT_VALID) {
        clear |= ((NSS_USE_DEFAULT_SSL_ENABLE | NSS_USE_DEFAULT_SMIME_ENABLE) &
                  ~set);
    }
    return NSS_SetAlgorithmPolicy(oid, set, clear);
}

/* apply the operator specific policy */
SECStatus
secmod_setPolicyOperation(SECOidTag oid, NSSPolicyOperation operation,
                          PRUint32 value)
{
    SECStatus rv = SECSuccess;
    switch (operation) {
        case NSS_DISALLOW:
            /* clear the requested policy bits */
            rv = NSS_SetAlgorithmPolicy(oid, 0, value);
            break;
        case NSS_ALLOW:
            /* set the requested policy bits */
            rv = NSS_SetAlgorithmPolicy(oid, value, 0);
            break;
        case NSS_DISABLE:
        case NSS_ENABLE:
            rv = secmod_setDefault(oid, operation, value);
            break;
        default:
            PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
            rv = SECFailure;
            break;
    }
    return rv;
}

const char *
secmod_getOperationString(NSSPolicyOperation operation)
{
    switch (operation) {
        case NSS_DISALLOW:
            return "disallow";
        case NSS_ALLOW:
            return "allow";
        case NSS_DISABLE:
            return "disable";
        case NSS_ENABLE:
            return "enable";
        default:
            break;
    }
    return "invalid";
}

/* Allow external applications fetch the policy oid based on the internal
 * string mapping used by the configuration system. The search can be
 * narrowed by supplying the name of the table (list) that the policy
 * is on. The value 'Any' allows the policy to be searched on all lists */
SECOidTag
SECMOD_PolicyStringToOid(const char *policy, const char *list)
{
    PRBool any = (PORT_Strcasecmp(list, "Any") == 0) ? PR_TRUE : PR_FALSE;
    int len = PORT_Strlen(policy);
    int i, j;

    for (i = 0; i < PR_ARRAY_SIZE(algOptLists); i++) {
        const algListsDef *algOptList = &algOptLists[i];
        if (any || (PORT_Strcasecmp(algOptList->description, list) == 0)) {
            for (j = 0; j < algOptList->entries; j++) {
                const oidValDef *algOpt = &algOptList->list[j];
                unsigned name_size = algOpt->name_size;
                if (len == name_size &&
                    PORT_Strcasecmp(algOpt->name, policy) == 0) {
                    return algOpt->oid;
                }
            }
        }
    }
    return SEC_OID_UNKNOWN;
}

/* Allow external applications fetch the NSS option based on the internal
 * string mapping used by the configuration system. */
PRUint32
SECMOD_PolicyStringToOpt(const char *policy)
{
    int len = PORT_Strlen(policy);
    int i;

    for (i = 0; i < PR_ARRAY_SIZE(freeOptList); i++) {
        const optionFreeDef *freeOpt = &freeOptList[i];
        unsigned name_size = freeOpt->name_size;
        if (len == name_size &&
            PORT_Strcasecmp(freeOpt->name, policy) == 0) {
            return freeOpt->option;
        }
    }
    return 0;
}

/* Allow external applications map policy flags to their string equivalance.
 * Some strings represent more than one flag. If more than one flag is included
 * the returned string is the string that contains any of the
 * supplied flags unless exact is specified. If exact is specified, then the
 * returned value matches all the included flags and only those flags. For
 * Example: 'ALL-SIGNATURE' has the bits NSS_USE_ALG_IN_CERTSIGNATURE|
 * NSS_USE_ALG_IN_SMIME_SIGNATURE|NSS_USE_ALG_IN_ANY_SIGNATURE. If you ask for
 * NSS_USE_ALG_IN_CERT_SIGNATURE|NSS_USE_ALG_IN_SMIME_SIGNATURE and don't set
 * exact, this function will return 'ALL-SIGNATURE' if you do set exact, you must
 * include all three bits in value to get 'All-SIGNATURE'*/
const char *
SECMOD_FlagsToPolicyString(PRUint32 val, PRBool exact)
{
    int i;

    for (i = 0; i < PR_ARRAY_SIZE(policyFlagList); i++) {
        const policyFlagDef *policy = &policyFlagList[i];
        if (exact && (policy->flag == val)) {
            return policy->name;
        }
        if (!exact && ((policy->flag & val) == policy->flag)) {
            return policy->name;
        }
    }
    return NULL;
}

static SECStatus
secmod_applyCryptoPolicy(const char *policyString, NSSPolicyOperation operation,
                         PRBool printPolicyFeedback, PRUint32 policyCheckFlags)
{
    const char *cipher, *currentString;
    unsigned i, j;
    SECStatus rv = SECSuccess;
    PRBool unknown;

    if (policyString == NULL || policyString[0] == 0) {
        return SECSuccess; /* do nothing */
    }

    /* if we change any of these, make sure it gets applied in ssl as well */
    NSS_SetAlgorithmPolicy(SEC_OID_APPLY_SSL_POLICY, NSS_USE_POLICY_IN_SSL, 0);

    for (currentString = policyString; currentString;) {
        int length;
        PRBool newValue = PR_FALSE;

        cipher = secmod_ArgGetSubValue(currentString, ':', 0, &length,
                                       &currentString);
        unknown = PR_TRUE;
        if (length >= 3 && cipher[3] == '/') {
            newValue = PR_TRUE;
        }
        if ((newValue || (length == 3)) && PORT_Strncasecmp(cipher, "all", 3) == 0) {
            /* disable or enable all options by default */
            PRUint32 value = 0;
            if (newValue) {
                value = secmod_parsePolicyValue(&cipher[3] + 1, length - 3 - 1, printPolicyFeedback, policyCheckFlags);
            }
            for (i = 0; i < PR_ARRAY_SIZE(algOptLists); i++) {
                const algListsDef *algOptList = &algOptLists[i];
                for (j = 0; j < algOptList->entries; j++) {
                    if (!newValue) {
                        value = algOptList->list[j].val;
                    }
                    secmod_setPolicyOperation(algOptList->list[j].oid, operation, value);
                }
            }
            continue;
        }

        for (i = 0; i < PR_ARRAY_SIZE(algOptLists); i++) {
            const algListsDef *algOptList = &algOptLists[i];
            for (j = 0; j < algOptList->entries; j++) {
                const oidValDef *algOpt = &algOptList->list[j];
                unsigned name_size = algOpt->name_size;
                PRBool newOption = PR_FALSE;

                if ((length >= name_size) && (cipher[name_size] == '/')) {
                    newOption = PR_TRUE;
                }
                if ((newOption || algOpt->name_size == length) &&
                    PORT_Strncasecmp(algOpt->name, cipher, name_size) == 0) {
                    PRUint32 value = algOpt->val;
                    if (newOption) {
                        value = secmod_parsePolicyValue(&cipher[name_size] + 1,
                                                        length - name_size - 1,
                                                        printPolicyFeedback,
                                                        policyCheckFlags);
                    }
                    rv = secmod_setPolicyOperation(algOptList->list[j].oid, operation, value);
                    if (rv != SECSuccess) {
                        /* could not enable option */
                        /* NSS_SetAlgorithPolicy should have set the error code */
                        return SECFailure;
                    }
                    unknown = PR_FALSE;
                    break;
                }
            }
        }
        if (!unknown) {
            continue;
        }

        for (i = 0; i < PR_ARRAY_SIZE(freeOptList); i++) {
            const optionFreeDef *freeOpt = &freeOptList[i];
            unsigned name_size = freeOpt->name_size;

            if ((length > name_size) && cipher[name_size] == '=' &&
                PORT_Strncasecmp(freeOpt->name, cipher, name_size) == 0) {
                PRInt32 val;
                const char *policyValue = &cipher[name_size + 1];
                int policyValueLength = length - name_size - 1;
                rv = secmod_getPolicyOptValue(policyValue, policyValueLength,
                                              &val);
                if (rv != SECSuccess) {
                    if (printPolicyFeedback &&
                        (policyCheckFlags & SECMOD_FLAG_POLICY_CHECK_VALUE)) {
                        PR_SetEnv("NSS_POLICY_FAIL=1");
                        fprintf(stderr, "NSS-POLICY-FAIL %.*s: unknown value: %.*s\n",
                                length, cipher, policyValueLength, policyValue);
                    }
                    return SECFailure;
                }
                rv = NSS_OptionSet(freeOpt->option, val);
                if (rv != SECSuccess) {
                    /* could not enable option */
                    /* NSS_OptionSet should have set the error code */
                    return SECFailure;
                }
                /* to allow the policy to expand in the future. ignore ciphers
                 * we don't understand */
                unknown = PR_FALSE;
                break;
            }
        }

        if (unknown && printPolicyFeedback &&
            (policyCheckFlags & SECMOD_FLAG_POLICY_CHECK_IDENTIFIER)) {
            PR_SetEnv("NSS_POLICY_FAIL=1");
            fprintf(stderr, "NSS-POLICY-FAIL %s: unknown identifier: %.*s\n",
                    secmod_getOperationString(operation), length, cipher);
        }
    }
    return rv;
}

static void
secmod_sanityCheckCryptoPolicy(void)
{
    unsigned i, j;
    SECStatus rv = SECSuccess;
    unsigned num_kx_enabled = 0;
    unsigned num_ssl_enabled = 0;
    unsigned num_sig_enabled = 0;
    unsigned enabledCount[PR_ARRAY_SIZE(algOptLists)];
    const char *sWarn = "WARN";
    const char *sInfo = "INFO";
    PRBool haveWarning = PR_FALSE;

    for (i = 0; i < PR_ARRAY_SIZE(algOptLists); i++) {
        const algListsDef *algOptList = &algOptLists[i];
        enabledCount[i] = 0;
        for (j = 0; j < algOptList->entries; j++) {
            const oidValDef *algOpt = &algOptList->list[j];
            PRUint32 value;
            PRBool anyEnabled = PR_FALSE;
            rv = NSS_GetAlgorithmPolicy(algOpt->oid, &value);
            if (rv != SECSuccess) {
                PR_SetEnv("NSS_POLICY_FAIL=1");
                fprintf(stderr, "NSS-POLICY-FAIL: internal failure with NSS_GetAlgorithmPolicy at %u\n", i);
                return;
            }

            if ((algOpt->val & NSS_USE_ALG_IN_SSL_KX) && (value & NSS_USE_ALG_IN_SSL_KX)) {
                ++num_kx_enabled;
                anyEnabled = PR_TRUE;
                fprintf(stderr, "NSS-POLICY-INFO: %s is enabled for SSL-KX\n", algOpt->name);
            }
            if ((algOpt->val & NSS_USE_ALG_IN_SSL) && (value & NSS_USE_ALG_IN_SSL)) {
                ++num_ssl_enabled;
                anyEnabled = PR_TRUE;
                fprintf(stderr, "NSS-POLICY-INFO: %s is enabled for SSL\n", algOpt->name);
            }
            if ((algOpt->val & NSS_USE_ALG_IN_CERT_SIGNATURE) &&
                ((value & NSS_USE_CERT_SIGNATURE_OK) == NSS_USE_CERT_SIGNATURE_OK)) {
                ++num_sig_enabled;
                anyEnabled = PR_TRUE;
                fprintf(stderr, "NSS-POLICY-INFO: %s is enabled for CERT-SIGNATURE\n", algOpt->name);
            }
            if (anyEnabled) {
                ++enabledCount[i];
            }
        }
    }
    fprintf(stderr, "NSS-POLICY-%s: NUMBER-OF-SSL-ALG-KX: %u\n", num_kx_enabled ? sInfo : sWarn, num_kx_enabled);
    fprintf(stderr, "NSS-POLICY-%s: NUMBER-OF-SSL-ALG: %u\n", num_ssl_enabled ? sInfo : sWarn, num_ssl_enabled);
    fprintf(stderr, "NSS-POLICY-%s: NUMBER-OF-CERT-SIG: %u\n", num_sig_enabled ? sInfo : sWarn, num_sig_enabled);
    if (!num_kx_enabled || !num_ssl_enabled || !num_sig_enabled) {
        haveWarning = PR_TRUE;
    }
    for (i = 0; i < PR_ARRAY_SIZE(algOptLists); i++) {
        const algListsDef *algOptList = &algOptLists[i];
        fprintf(stderr, "NSS-POLICY-%s: NUMBER-OF-%s: %u\n", enabledCount[i] ? sInfo : sWarn, algOptList->description, enabledCount[i]);
        if (!enabledCount[i] && !algOptList->allowEmpty) {
            haveWarning = PR_TRUE;
        }
    }
    if (haveWarning) {
        PR_SetEnv("NSS_POLICY_WARN=1");
    }
}

static SECStatus
secmod_parseCryptoPolicy(const char *policyConfig, PRBool printPolicyFeedback,
                         PRUint32 policyCheckFlags)
{
    char *args;
    SECStatus rv;

    if (policyConfig == NULL) {
        return SECSuccess; /* no policy given */
    }
    /* make sure we initialize the oid table and set all the default policy
     * values first so we can override them here */
    rv = SECOID_Init();
    if (rv != SECSuccess) {
        return rv;
    }
    args = NSSUTIL_ArgGetParamValue("disallow", policyConfig);
    rv = secmod_applyCryptoPolicy(args, NSS_DISALLOW, printPolicyFeedback,
                                  policyCheckFlags);
    if (args)
        PORT_Free(args);
    if (rv != SECSuccess) {
        return rv;
    }
    args = NSSUTIL_ArgGetParamValue("allow", policyConfig);
    rv = secmod_applyCryptoPolicy(args, NSS_ALLOW, printPolicyFeedback,
                                  policyCheckFlags);
    if (args)
        PORT_Free(args);
    if (rv != SECSuccess) {
        return rv;
    }
    args = NSSUTIL_ArgGetParamValue("disable", policyConfig);
    rv = secmod_applyCryptoPolicy(args, NSS_DISABLE, printPolicyFeedback,
                                  policyCheckFlags);
    if (args)
        PORT_Free(args);
    if (rv != SECSuccess) {
        return rv;
    }
    args = NSSUTIL_ArgGetParamValue("enable", policyConfig);
    rv = secmod_applyCryptoPolicy(args, NSS_ENABLE, printPolicyFeedback,
                                  policyCheckFlags);
    if (args)
        PORT_Free(args);
    if (rv != SECSuccess) {
        return rv;
    }
    /* this has to be last. Everything after this will be a noop */
    if (NSSUTIL_ArgHasFlag("flags", "ssl-lock", policyConfig)) {
        PRInt32 locks;
        /* don't overwrite other (future) lock flags */
        rv = NSS_OptionGet(NSS_DEFAULT_LOCKS, &locks);
        if (rv == SECSuccess) {
            rv = NSS_OptionSet(NSS_DEFAULT_LOCKS, locks | NSS_DEFAULT_SSL_LOCK);
        }
        if (rv != SECSuccess) {
            return rv;
        }
    }
    if (NSSUTIL_ArgHasFlag("flags", "policy-lock", policyConfig)) {
        NSS_LockPolicy();
    }
    if (printPolicyFeedback) {
        /* This helps to distinguish configurations that don't contain any
         * policy config= statement. */
        PR_SetEnv("NSS_POLICY_LOADED=1");
        fprintf(stderr, "NSS-POLICY-INFO: LOADED-SUCCESSFULLY\n");
        secmod_sanityCheckCryptoPolicy();
    }
    return rv;
}

static PRUint32
secmod_parsePolicyCheckFlags(const char *nss)
{
    PRUint32 policyCheckFlags = 0;

    if (NSSUTIL_ArgHasFlag("flags", "policyCheckIdentifier", nss)) {
        policyCheckFlags |= SECMOD_FLAG_POLICY_CHECK_IDENTIFIER;
    }

    if (NSSUTIL_ArgHasFlag("flags", "policyCheckValue", nss)) {
        policyCheckFlags |= SECMOD_FLAG_POLICY_CHECK_VALUE;
    }

    return policyCheckFlags;
}

/*
 * for 3.4 we continue to use the old SECMODModule structure
 */
SECMODModule *
SECMOD_CreateModuleEx(const char *library, const char *moduleName,
                      const char *parameters, const char *nss,
                      const char *config)
{
    SECMODModule *mod;
    SECStatus rv;
    char *slotParams, *ciphers;
    PRBool printPolicyFeedback = NSSUTIL_ArgHasFlag("flags", "printPolicyFeedback", nss);
    PRUint32 policyCheckFlags = secmod_parsePolicyCheckFlags(nss);

    rv = secmod_parseCryptoPolicy(config, printPolicyFeedback, policyCheckFlags);

    /* do not load the module if policy parsing fails */
    if (rv != SECSuccess) {
        if (printPolicyFeedback) {
            PR_SetEnv("NSS_POLICY_FAIL=1");
            fprintf(stderr, "NSS-POLICY-FAIL: policy config parsing failed, not loading module %s\n", moduleName);
        }
        return NULL;
    }

    mod = secmod_NewModule();
    if (mod == NULL)
        return NULL;

    mod->commonName = PORT_ArenaStrdup(mod->arena, moduleName ? moduleName : "");
    if (library) {
        mod->dllName = PORT_ArenaStrdup(mod->arena, library);
    }
    /* new field */
    if (parameters) {
        mod->libraryParams = PORT_ArenaStrdup(mod->arena, parameters);
    }

    mod->internal = NSSUTIL_ArgHasFlag("flags", "internal", nss);
    mod->isFIPS = NSSUTIL_ArgHasFlag("flags", "FIPS", nss);
    /* if the system FIPS mode is enabled, force FIPS to be on */
    if (SECMOD_GetSystemFIPSEnabled()) {
        mod->isFIPS = PR_TRUE;
    }
    mod->isCritical = NSSUTIL_ArgHasFlag("flags", "critical", nss);
    slotParams = NSSUTIL_ArgGetParamValue("slotParams", nss);
    mod->slotInfo = NSSUTIL_ArgParseSlotInfo(mod->arena, slotParams,
                                             &mod->slotInfoCount);
    if (slotParams)
        PORT_Free(slotParams);
    /* new field */
    mod->trustOrder = NSSUTIL_ArgReadLong("trustOrder", nss,
                                          NSSUTIL_DEFAULT_TRUST_ORDER, NULL);
    /* new field */
    mod->cipherOrder = NSSUTIL_ArgReadLong("cipherOrder", nss,
                                           NSSUTIL_DEFAULT_CIPHER_ORDER, NULL);
    /* new field */
    mod->isModuleDB = NSSUTIL_ArgHasFlag("flags", "moduleDB", nss);
    mod->moduleDBOnly = NSSUTIL_ArgHasFlag("flags", "moduleDBOnly", nss);
    if (mod->moduleDBOnly)
        mod->isModuleDB = PR_TRUE;

    /* we need more bits, but we also want to preserve binary compatibility
     * so we overload the isModuleDB PRBool with additional flags.
     * These flags are only valid if mod->isModuleDB is already set.
     * NOTE: this depends on the fact that PRBool is at least a char on
     * all platforms. These flags are only valid if moduleDB is set, so
     * code checking if (mod->isModuleDB) will continue to work correctly. */
    if (mod->isModuleDB) {
        char flags = SECMOD_FLAG_MODULE_DB_IS_MODULE_DB;
        if (NSSUTIL_ArgHasFlag("flags", "skipFirst", nss)) {
            flags |= SECMOD_FLAG_MODULE_DB_SKIP_FIRST;
        }
        if (NSSUTIL_ArgHasFlag("flags", "defaultModDB", nss)) {
            flags |= SECMOD_FLAG_MODULE_DB_DEFAULT_MODDB;
        }
        if (NSSUTIL_ArgHasFlag("flags", "policyOnly", nss)) {
            flags |= SECMOD_FLAG_MODULE_DB_POLICY_ONLY;
        }
        /* additional moduleDB flags could be added here in the future */
        mod->isModuleDB = (PRBool)flags;
    }

    if (mod->internal) {
        char flags = SECMOD_FLAG_INTERNAL_IS_INTERNAL;

        if (NSSUTIL_ArgHasFlag("flags", "internalKeySlot", nss)) {
            flags |= SECMOD_FLAG_INTERNAL_KEY_SLOT;
        }
        mod->internal = (PRBool)flags;
    }

    ciphers = NSSUTIL_ArgGetParamValue("ciphers", nss);
    NSSUTIL_ArgParseCipherFlags(&mod->ssl[0], ciphers);
    if (ciphers)
        PORT_Free(ciphers);

    secmod_PrivateModuleCount++;

    return mod;
}

PRBool
SECMOD_GetSkipFirstFlag(SECMODModule *mod)
{
    char flags = (char)mod->isModuleDB;

    return (flags & SECMOD_FLAG_MODULE_DB_SKIP_FIRST) ? PR_TRUE : PR_FALSE;
}

PRBool
SECMOD_GetDefaultModDBFlag(SECMODModule *mod)
{
    char flags = (char)mod->isModuleDB;

    return (flags & SECMOD_FLAG_MODULE_DB_DEFAULT_MODDB) ? PR_TRUE : PR_FALSE;
}

PRBool
secmod_PolicyOnly(SECMODModule *mod)
{
    char flags = (char)mod->isModuleDB;

    return (flags & SECMOD_FLAG_MODULE_DB_POLICY_ONLY) ? PR_TRUE : PR_FALSE;
}

PRBool
secmod_IsInternalKeySlot(SECMODModule *mod)
{
    char flags = (char)mod->internal;

    return (flags & SECMOD_FLAG_INTERNAL_KEY_SLOT) ? PR_TRUE : PR_FALSE;
}

void
secmod_SetInternalKeySlotFlag(SECMODModule *mod, PRBool val)
{
    char flags = (char)mod->internal;

    if (val) {
        flags |= SECMOD_FLAG_INTERNAL_KEY_SLOT;
    } else {
        flags &= ~SECMOD_FLAG_INTERNAL_KEY_SLOT;
    }
    mod->internal = flags;
}

/*
 * copy desc and value into target. Target is known to be big enough to
 * hold desc +2 +value, which is good because the result of this will be
 * *desc"*value". We may, however, have to add some escapes for special
 * characters imbedded into value (rare). This string potentially comes from
 * a user, so we don't want the user overflowing the target buffer by using
 * excessive escapes. To prevent this we count the escapes we need to add and
 * try to expand the buffer with Realloc.
 */
static char *
secmod_doDescCopy(char *target, char **base, int *baseLen,
                  const char *desc, int descLen, char *value)
{
    int diff, esc_len;

    esc_len = NSSUTIL_EscapeSize(value, '\"') - 1;
    diff = esc_len - strlen(value);
    if (diff > 0) {
        /* we need to escape... expand newSpecPtr as well to make sure
         * we don't overflow it */
        int offset = target - *base;
        char *newPtr = PORT_Realloc(*base, *baseLen + diff);
        if (!newPtr) {
            return target; /* not enough space, just drop the whole copy */
        }
        *baseLen += diff;
        target = newPtr + offset;
        *base = newPtr;
        value = NSSUTIL_Escape(value, '\"');
        if (value == NULL) {
            return target; /* couldn't escape value, just drop the copy */
        }
    }
    PORT_Memcpy(target, desc, descLen);
    target += descLen;
    *target++ = '\"';
    PORT_Memcpy(target, value, esc_len);
    target += esc_len;
    *target++ = '\"';
    if (diff > 0) {
        PORT_Free(value);
    }
    return target;
}

#define SECMOD_SPEC_COPY(new, start, end) \
    if (end > start) {                    \
        int _cnt = end - start;           \
        PORT_Memcpy(new, start, _cnt);    \
        new += _cnt;                      \
    }
#define SECMOD_TOKEN_DESCRIPTION "tokenDescription="
#define SECMOD_SLOT_DESCRIPTION "slotDescription="

/*
 * Find any tokens= values in the module spec.
 * Always return a new spec which does not have any tokens= arguments.
 * If tokens= arguments are found, Split the the various tokens defined into
 * an array of child specs to return.
 *
 * Caller is responsible for freeing the child spec and the new token
 * spec.
 */
char *
secmod_ParseModuleSpecForTokens(PRBool convert, PRBool isFIPS,
                                const char *moduleSpec, char ***children,
                                CK_SLOT_ID **ids)
{
    int newSpecLen = PORT_Strlen(moduleSpec) + 2;
    char *newSpec = PORT_Alloc(newSpecLen);
    char *newSpecPtr = newSpec;
    const char *modulePrev = moduleSpec;
    char *target = NULL;
    char *tmp = NULL;
    char **childArray = NULL;
    const char *tokenIndex;
    CK_SLOT_ID *idArray = NULL;
    int tokenCount = 0;
    int i;

    if (newSpec == NULL) {
        return NULL;
    }

    *children = NULL;
    if (ids) {
        *ids = NULL;
    }
    moduleSpec = NSSUTIL_ArgStrip(moduleSpec);
    SECMOD_SPEC_COPY(newSpecPtr, modulePrev, moduleSpec);

    /* Notes on 'convert' and 'isFIPS' flags: The base parameters for opening
     * a new softoken module takes the following parameters to name the
     * various tokens:
     *
     *  cryptoTokenDescription: name of the non-fips crypto token.
     *  cryptoSlotDescription: name of the non-fips crypto slot.
     *  dbTokenDescription: name of the non-fips db token.
     *  dbSlotDescription: name of the non-fips db slot.
     *  FIPSTokenDescription: name of the fips db/crypto token.
     *  FIPSSlotDescription: name of the fips db/crypto slot.
     *
     * if we are opening a new slot, we need to have the following
     * parameters:
     *  tokenDescription: name of the token.
     *  slotDescription: name of the slot.
     *
     *
     * The convert flag tells us to drop the unnecessary *TokenDescription
     * and *SlotDescription arguments and convert the appropriate pair
     * (either db or FIPS based on the isFIPS flag) to tokenDescription and
     * slotDescription).
     */
    /*
     * walk down the list. if we find a tokens= argument, save it,
     * otherise copy the argument.
     */
    while (*moduleSpec) {
        int next;
        modulePrev = moduleSpec;
        NSSUTIL_HANDLE_STRING_ARG(moduleSpec, target, "tokens=",
                                  modulePrev = moduleSpec;
                                  /* skip copying */)
        NSSUTIL_HANDLE_STRING_ARG(
            moduleSpec, tmp, "cryptoTokenDescription=",
            if (convert) { modulePrev = moduleSpec; })
        NSSUTIL_HANDLE_STRING_ARG(
            moduleSpec, tmp, "cryptoSlotDescription=",
            if (convert) { modulePrev = moduleSpec; })
        NSSUTIL_HANDLE_STRING_ARG(
            moduleSpec, tmp, "dbTokenDescription=",
            if (convert) {
                modulePrev = moduleSpec;
                if (!isFIPS) {
                    newSpecPtr = secmod_doDescCopy(newSpecPtr,
                                                   &newSpec, &newSpecLen,
                                                   SECMOD_TOKEN_DESCRIPTION,
                                                   sizeof(SECMOD_TOKEN_DESCRIPTION) - 1,
                                                   tmp);
                }
            })
        NSSUTIL_HANDLE_STRING_ARG(
            moduleSpec, tmp, "dbSlotDescription=",
            if (convert) {
                modulePrev = moduleSpec; /* skip copying */
                if (!isFIPS) {
                    newSpecPtr = secmod_doDescCopy(newSpecPtr,
                                                   &newSpec, &newSpecLen,
                                                   SECMOD_SLOT_DESCRIPTION,
                                                   sizeof(SECMOD_SLOT_DESCRIPTION) - 1,
                                                   tmp);
                }
            })
        NSSUTIL_HANDLE_STRING_ARG(
            moduleSpec, tmp, "FIPSTokenDescription=",
            if (convert) {
                modulePrev = moduleSpec; /* skip copying */
                if (isFIPS) {
                    newSpecPtr = secmod_doDescCopy(newSpecPtr,
                                                   &newSpec, &newSpecLen,
                                                   SECMOD_TOKEN_DESCRIPTION,
                                                   sizeof(SECMOD_TOKEN_DESCRIPTION) - 1,
                                                   tmp);
                }
            })
        NSSUTIL_HANDLE_STRING_ARG(
            moduleSpec, tmp, "FIPSSlotDescription=",
            if (convert) {
                modulePrev = moduleSpec; /* skip copying */
                if (isFIPS) {
                    newSpecPtr = secmod_doDescCopy(newSpecPtr,
                                                   &newSpec, &newSpecLen,
                                                   SECMOD_SLOT_DESCRIPTION,
                                                   sizeof(SECMOD_SLOT_DESCRIPTION) - 1,
                                                   tmp);
                }
            })
        NSSUTIL_HANDLE_FINAL_ARG(moduleSpec)
        SECMOD_SPEC_COPY(newSpecPtr, modulePrev, moduleSpec);
    }
    if (tmp) {
        PORT_Free(tmp);
        tmp = NULL;
    }
    *newSpecPtr = 0;

    /* no target found, return the newSpec */
    if (target == NULL) {
        return newSpec;
    }

    /* now build the child array from target */
    /*first count them */
    for (tokenIndex = NSSUTIL_ArgStrip(target); *tokenIndex;
         tokenIndex = NSSUTIL_ArgStrip(NSSUTIL_ArgSkipParameter(tokenIndex))) {
        tokenCount++;
    }

    childArray = PORT_NewArray(char *, tokenCount + 1);
    if (childArray == NULL) {
        /* just return the spec as is then */
        PORT_Free(target);
        return newSpec;
    }
    if (ids) {
        idArray = PORT_NewArray(CK_SLOT_ID, tokenCount + 1);
        if (idArray == NULL) {
            PORT_Free(childArray);
            PORT_Free(target);
            return newSpec;
        }
    }

    /* now fill them in */
    for (tokenIndex = NSSUTIL_ArgStrip(target), i = 0;
         *tokenIndex && (i < tokenCount);
         tokenIndex = NSSUTIL_ArgStrip(tokenIndex)) {
        int next;
        char *name = NSSUTIL_ArgGetLabel(tokenIndex, &next);
        tokenIndex += next;

        if (idArray) {
            idArray[i] = NSSUTIL_ArgDecodeNumber(name);
        }

        PORT_Free(name); /* drop the explicit number */

        /* if anything is left, copy the args to the child array */
        if (!NSSUTIL_ArgIsBlank(*tokenIndex)) {
            childArray[i++] = NSSUTIL_ArgFetchValue(tokenIndex, &next);
            tokenIndex += next;
        }
    }

    PORT_Free(target);
    childArray[i] = 0;
    if (idArray) {
        idArray[i] = 0;
    }

    /* return it */
    *children = childArray;
    if (ids) {
        *ids = idArray;
    }
    return newSpec;
}

/* get the database and flags from the spec */
static char *
secmod_getConfigDir(const char *spec, char **certPrefix, char **keyPrefix,
                    PRBool *readOnly)
{
    char *config = NULL;

    *certPrefix = NULL;
    *keyPrefix = NULL;
    *readOnly = NSSUTIL_ArgHasFlag("flags", "readOnly", spec);
    if (NSSUTIL_ArgHasFlag("flags", "nocertdb", spec) ||
        NSSUTIL_ArgHasFlag("flags", "nokeydb", spec)) {
        return NULL;
    }

    spec = NSSUTIL_ArgStrip(spec);
    while (*spec) {
        int next;
        NSSUTIL_HANDLE_STRING_ARG(spec, config, "configdir=", ;)
        NSSUTIL_HANDLE_STRING_ARG(spec, *certPrefix, "certPrefix=", ;)
        NSSUTIL_HANDLE_STRING_ARG(spec, *keyPrefix, "keyPrefix=", ;)
        NSSUTIL_HANDLE_FINAL_ARG(spec)
    }
    return config;
}

struct SECMODConfigListStr {
    char *config;
    char *certPrefix;
    char *keyPrefix;
    PRBool isReadOnly;
};

/*
 * return an array of already openned databases from a spec list.
 */
SECMODConfigList *
secmod_GetConfigList(PRBool isFIPS, char *spec, int *count)
{
    char **children;
    CK_SLOT_ID *ids;
    char *strippedSpec;
    int childCount;
    SECMODConfigList *conflist = NULL;
    int i;

    strippedSpec = secmod_ParseModuleSpecForTokens(PR_TRUE, isFIPS,
                                                   spec, &children, &ids);
    if (strippedSpec == NULL) {
        return NULL;
    }

    for (childCount = 0; children && children[childCount]; childCount++)
        ;
    *count = childCount + 1; /* include strippedSpec */
    conflist = PORT_NewArray(SECMODConfigList, *count);
    if (conflist == NULL) {
        *count = 0;
        goto loser;
    }

    conflist[0].config = secmod_getConfigDir(strippedSpec,
                                             &conflist[0].certPrefix,
                                             &conflist[0].keyPrefix,
                                             &conflist[0].isReadOnly);
    for (i = 0; i < childCount; i++) {
        conflist[i + 1].config = secmod_getConfigDir(children[i],
                                                     &conflist[i + 1].certPrefix,
                                                     &conflist[i + 1].keyPrefix,
                                                     &conflist[i + 1].isReadOnly);
    }

loser:
    secmod_FreeChildren(children, ids);
    PORT_Free(strippedSpec);
    return conflist;
}

/*
 * determine if we are trying to open an old dbm database. For this test
 * RDB databases should return PR_FALSE.
 */
static PRBool
secmod_configIsDBM(char *configDir)
{
    char *env;

    /* explicit dbm open */
    if (strncmp(configDir, "dbm:", 4) == 0) {
        return PR_TRUE;
    }
    /* explicit open of a non-dbm database */
    if ((strncmp(configDir, "sql:", 4) == 0) ||
        (strncmp(configDir, "rdb:", 4) == 0) ||
        (strncmp(configDir, "extern:", 7) == 0)) {
        return PR_FALSE;
    }
    env = PR_GetEnvSecure("NSS_DEFAULT_DB_TYPE");
    /* implicit dbm open */
    if ((env == NULL) || (strcmp(env, "dbm") == 0)) {
        return PR_TRUE;
    }
    /* implicit non-dbm open */
    return PR_FALSE;
}

/*
 * match two prefixes. prefix may be NULL. NULL patches '\0'
 */
static PRBool
secmod_matchPrefix(char *prefix1, char *prefix2)
{
    if ((prefix1 == NULL) || (*prefix1 == 0)) {
        if ((prefix2 == NULL) || (*prefix2 == 0)) {
            return PR_TRUE;
        }
        return PR_FALSE;
    }
    if (strcmp(prefix1, prefix2) == 0) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

/* do two config paramters match? Not all callers are compariing
 * SECMODConfigLists directly, so this function breaks them out to their
 * components. */
static PRBool
secmod_matchConfig(char *configDir1, char *configDir2,
                   char *certPrefix1, char *certPrefix2,
                   char *keyPrefix1, char *keyPrefix2,
                   PRBool isReadOnly1, PRBool isReadOnly2)
{
    /* TODO: Document the answer to the question:
     *       "Why not allow them to match if they are both NULL?"
     * See: https://bugzilla.mozilla.org/show_bug.cgi?id=1318633#c1
     */
    if ((configDir1 == NULL) || (configDir2 == NULL)) {
        return PR_FALSE;
    }
    if (strcmp(configDir1, configDir2) != 0) {
        return PR_FALSE;
    }
    if (!secmod_matchPrefix(certPrefix1, certPrefix2)) {
        return PR_FALSE;
    }
    if (!secmod_matchPrefix(keyPrefix1, keyPrefix2)) {
        return PR_FALSE;
    }
    /* these last test -- if we just need the DB open read only,
     * than any open will suffice, but if we requested it read/write
     * and it's only open read only, we need to open it again */
    if (isReadOnly1) {
        return PR_TRUE;
    }
    if (isReadOnly2) { /* isReadonly1 == PR_FALSE */
        return PR_FALSE;
    }
    return PR_TRUE;
}

/*
 * return true if we are requesting a database that is already openned.
 */
PRBool
secmod_MatchConfigList(const char *spec, SECMODConfigList *conflist, int count)
{
    char *config;
    char *certPrefix;
    char *keyPrefix;
    PRBool isReadOnly;
    PRBool ret = PR_FALSE;
    int i;

    config = secmod_getConfigDir(spec, &certPrefix, &keyPrefix, &isReadOnly);
    if (!config) {
        goto done;
    }

    /* NOTE: we dbm isn't multiple open safe. If we open the same database
     * twice from two different locations, then we can corrupt our database
     * (the cache will be inconsistent). Protect against this by claiming
     * for comparison only that we are always openning dbm databases read only.
     */
    if (secmod_configIsDBM(config)) {
        isReadOnly = 1;
    }
    for (i = 0; i < count; i++) {
        if (secmod_matchConfig(config, conflist[i].config, certPrefix,
                               conflist[i].certPrefix, keyPrefix,
                               conflist[i].keyPrefix, isReadOnly,
                               conflist[i].isReadOnly)) {
            ret = PR_TRUE;
            goto done;
        }
    }

    ret = PR_FALSE;
done:
    PORT_Free(config);
    PORT_Free(certPrefix);
    PORT_Free(keyPrefix);
    return ret;
}

/*
 * Find the slot id from the module spec. If the slot is the database slot, we
 * can get the slot id from the default database slot.
 */
CK_SLOT_ID
secmod_GetSlotIDFromModuleSpec(const char *moduleSpec, SECMODModule *module)
{
    char *tmp_spec = NULL;
    char **children, **thisChild;
    CK_SLOT_ID *ids, *thisID, slotID = -1;
    char *inConfig = NULL, *thisConfig = NULL;
    char *inCertPrefix = NULL, *thisCertPrefix = NULL;
    char *inKeyPrefix = NULL, *thisKeyPrefix = NULL;
    PRBool inReadOnly, thisReadOnly;

    inConfig = secmod_getConfigDir(moduleSpec, &inCertPrefix, &inKeyPrefix,
                                   &inReadOnly);
    if (!inConfig) {
        goto done;
    }

    if (secmod_configIsDBM(inConfig)) {
        inReadOnly = 1;
    }

    tmp_spec = secmod_ParseModuleSpecForTokens(PR_TRUE, module->isFIPS,
                                               module->libraryParams, &children, &ids);
    if (tmp_spec == NULL) {
        goto done;
    }

    /* first check to see if the parent is the database */
    thisConfig = secmod_getConfigDir(tmp_spec, &thisCertPrefix, &thisKeyPrefix,
                                     &thisReadOnly);
    if (!thisConfig) {
        goto done;
    }
    if (secmod_matchConfig(inConfig, thisConfig, inCertPrefix, thisCertPrefix,
                           inKeyPrefix, thisKeyPrefix, inReadOnly, thisReadOnly)) {
        /* yup it's the default key slot, get the id for it */
        PK11SlotInfo *slot = PK11_GetInternalKeySlot();
        if (slot) {
            slotID = slot->slotID;
            PK11_FreeSlot(slot);
        }
        goto done;
    }

    /* find id of the token */
    for (thisChild = children, thisID = ids; thisChild && *thisChild; thisChild++, thisID++) {
        PORT_Free(thisConfig);
        PORT_Free(thisCertPrefix);
        PORT_Free(thisKeyPrefix);
        thisConfig = secmod_getConfigDir(*thisChild, &thisCertPrefix,
                                         &thisKeyPrefix, &thisReadOnly);
        if (thisConfig == NULL) {
            continue;
        }
        if (secmod_matchConfig(inConfig, thisConfig, inCertPrefix, thisCertPrefix,
                               inKeyPrefix, thisKeyPrefix, inReadOnly, thisReadOnly)) {
            slotID = *thisID;
            break;
        }
    }

done:
    PORT_Free(inConfig);
    PORT_Free(inCertPrefix);
    PORT_Free(inKeyPrefix);
    PORT_Free(thisConfig);
    PORT_Free(thisCertPrefix);
    PORT_Free(thisKeyPrefix);
    if (tmp_spec) {
        secmod_FreeChildren(children, ids);
        PORT_Free(tmp_spec);
    }
    return slotID;
}

void
secmod_FreeConfigList(SECMODConfigList *conflist, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        PORT_Free(conflist[i].config);
        PORT_Free(conflist[i].certPrefix);
        PORT_Free(conflist[i].keyPrefix);
    }
    PORT_Free(conflist);
}

void
secmod_FreeChildren(char **children, CK_SLOT_ID *ids)
{
    char **thisChild;

    if (!children) {
        return;
    }

    for (thisChild = children; thisChild && *thisChild; thisChild++) {
        PORT_Free(*thisChild);
    }
    PORT_Free(children);
    if (ids) {
        PORT_Free(ids);
    }
    return;
}

/*
 * caclulate the length of each child record:
 * " 0x{id}=<{escaped_child}>"
 */
static int
secmod_getChildLength(char *child, CK_SLOT_ID id)
{
    int length = NSSUTIL_DoubleEscapeSize(child, '>', ']');
    if (id == 0) {
        length++;
    }
    while (id) {
        length++;
        id = id >> 4;
    }
    length += 6; /* {sp}0x[id]=<{child}> */
    return length;
}

/*
 * Build a child record:
 * " 0x{id}=<{escaped_child}>"
 */
static SECStatus
secmod_mkTokenChild(char **next, int *length, char *child, CK_SLOT_ID id)
{
    int len;
    char *escSpec;

    len = PR_snprintf(*next, *length, " 0x%x=<", id);
    if (len < 0) {
        return SECFailure;
    }
    *next += len;
    *length -= len;
    escSpec = NSSUTIL_DoubleEscape(child, '>', ']');
    if (escSpec == NULL) {
        return SECFailure;
    }
    if (*child && (*escSpec == 0)) {
        PORT_Free(escSpec);
        return SECFailure;
    }
    len = strlen(escSpec);
    if (len + 1 > *length) {
        PORT_Free(escSpec);
        return SECFailure;
    }
    PORT_Memcpy(*next, escSpec, len);
    *next += len;
    *length -= len;
    PORT_Free(escSpec);
    **next = '>';
    (*next)++;
    (*length)--;
    return SECSuccess;
}

#define TOKEN_STRING " tokens=["

char *
secmod_MkAppendTokensList(PLArenaPool *arena, char *oldParam, char *newToken,
                          CK_SLOT_ID newID, char **children, CK_SLOT_ID *ids)
{
    char *rawParam = NULL;  /* oldParam with tokens stripped off */
    char *newParam = NULL;  /* space for the return parameter */
    char *nextParam = NULL; /* current end of the new parameter */
    char **oldChildren = NULL;
    CK_SLOT_ID *oldIds = NULL;
    void *mark = NULL; /* mark the arena pool in case we need
                        * to release it */
    int length, i, tmpLen;
    SECStatus rv;

    /* first strip out and save the old tokenlist */
    rawParam = secmod_ParseModuleSpecForTokens(PR_FALSE, PR_FALSE,
                                               oldParam, &oldChildren, &oldIds);
    if (!rawParam) {
        goto loser;
    }

    /* now calculate the total length of the new buffer */
    /* First the 'fixed stuff', length of rawparam (does not include a NULL),
     * length of the token string (does include the NULL), closing bracket */
    length = strlen(rawParam) + sizeof(TOKEN_STRING) + 1;
    /* now add then length of all the old children */
    for (i = 0; oldChildren && oldChildren[i]; i++) {
        length += secmod_getChildLength(oldChildren[i], oldIds[i]);
    }

    /* add the new token */
    length += secmod_getChildLength(newToken, newID);

    /* and it's new children */
    for (i = 0; children && children[i]; i++) {
        if (ids[i] == -1) {
            continue;
        }
        length += secmod_getChildLength(children[i], ids[i]);
    }

    /* now allocate and build the string */
    mark = PORT_ArenaMark(arena);
    if (!mark) {
        goto loser;
    }
    newParam = PORT_ArenaAlloc(arena, length);
    if (!newParam) {
        goto loser;
    }

    PORT_Strcpy(newParam, oldParam);
    tmpLen = strlen(oldParam);
    nextParam = newParam + tmpLen;
    length -= tmpLen;
    PORT_Memcpy(nextParam, TOKEN_STRING, sizeof(TOKEN_STRING) - 1);
    nextParam += sizeof(TOKEN_STRING) - 1;
    length -= sizeof(TOKEN_STRING) - 1;

    for (i = 0; oldChildren && oldChildren[i]; i++) {
        rv = secmod_mkTokenChild(&nextParam, &length, oldChildren[i], oldIds[i]);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    rv = secmod_mkTokenChild(&nextParam, &length, newToken, newID);
    if (rv != SECSuccess) {
        goto loser;
    }

    for (i = 0; children && children[i]; i++) {
        if (ids[i] == -1) {
            continue;
        }
        rv = secmod_mkTokenChild(&nextParam, &length, children[i], ids[i]);
        if (rv != SECSuccess) {
            goto loser;
        }
    }

    if (length < 2) {
        goto loser;
    }

    *nextParam++ = ']';
    *nextParam++ = 0;

    /* we are going to return newParam now, don't release the mark */
    PORT_ArenaUnmark(arena, mark);
    mark = NULL;

loser:
    if (mark) {
        PORT_ArenaRelease(arena, mark);
        newParam = NULL; /* if the mark is still active,
                          * don't return the param */
    }
    if (rawParam) {
        PORT_Free(rawParam);
    }
    if (oldChildren) {
        secmod_FreeChildren(oldChildren, oldIds);
    }
    return newParam;
}

static char *
secmod_mkModuleSpec(SECMODModule *module)
{
    char *nss = NULL, *modSpec = NULL, **slotStrings = NULL;
    int slotCount, i, si;
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();

    /* allocate target slot info strings */
    slotCount = 0;

    SECMOD_GetReadLock(moduleLock);
    if (module->slotCount) {
        for (i = 0; i < module->slotCount; i++) {
            if (module->slots[i]->defaultFlags != 0) {
                slotCount++;
            }
        }
    } else {
        slotCount = module->slotInfoCount;
    }

    slotStrings = (char **)PORT_ZAlloc(slotCount * sizeof(char *));
    if (slotStrings == NULL) {
        SECMOD_ReleaseReadLock(moduleLock);
        goto loser;
    }

    /* build the slot info strings */
    if (module->slotCount) {
        for (i = 0, si = 0; i < module->slotCount; i++) {
            if (module->slots[i]->defaultFlags) {
                PORT_Assert(si < slotCount);
                if (si >= slotCount)
                    break;
                slotStrings[si] = NSSUTIL_MkSlotString(module->slots[i]->slotID,
                                                       module->slots[i]->defaultFlags,
                                                       module->slots[i]->timeout,
                                                       module->slots[i]->askpw,
                                                       module->slots[i]->hasRootCerts,
                                                       module->slots[i]->hasRootTrust);
                si++;
            }
        }
    } else {
        for (i = 0; i < slotCount; i++) {
            slotStrings[i] = NSSUTIL_MkSlotString(
                module->slotInfo[i].slotID,
                module->slotInfo[i].defaultFlags,
                module->slotInfo[i].timeout,
                module->slotInfo[i].askpw,
                module->slotInfo[i].hasRootCerts,
                module->slotInfo[i].hasRootTrust);
        }
    }

    SECMOD_ReleaseReadLock(moduleLock);
    nss = NSSUTIL_MkNSSString(slotStrings, slotCount, module->internal,
                              module->isFIPS, module->isModuleDB,
                              module->moduleDBOnly, module->isCritical,
                              module->trustOrder, module->cipherOrder,
                              module->ssl[0], module->ssl[1]);
    modSpec = NSSUTIL_MkModuleSpec(module->dllName, module->commonName,
                                   module->libraryParams, nss);
    PORT_Free(slotStrings);
    PR_smprintf_free(nss);
loser:
    return (modSpec);
}

char **
SECMOD_GetModuleSpecList(SECMODModule *module)
{
    SECMODModuleDBFunc func = (SECMODModuleDBFunc)module->moduleDBFunc;
    if (func) {
        return (*func)(SECMOD_MODULE_DB_FUNCTION_FIND,
                       module->libraryParams, NULL);
    }
    return NULL;
}

SECStatus
SECMOD_AddPermDB(SECMODModule *module)
{
    SECMODModuleDBFunc func;
    char *moduleSpec;
    char **retString;

    if (module->parent == NULL)
        return SECFailure;

    func = (SECMODModuleDBFunc)module->parent->moduleDBFunc;
    if (func) {
        moduleSpec = secmod_mkModuleSpec(module);
        retString = (*func)(SECMOD_MODULE_DB_FUNCTION_ADD,
                            module->parent->libraryParams, moduleSpec);
        PORT_Free(moduleSpec);
        if (retString != NULL)
            return SECSuccess;
    }
    return SECFailure;
}

SECStatus
SECMOD_DeletePermDB(SECMODModule *module)
{
    SECMODModuleDBFunc func;
    char *moduleSpec;
    char **retString;

    if (module->parent == NULL)
        return SECFailure;

    func = (SECMODModuleDBFunc)module->parent->moduleDBFunc;
    if (func) {
        moduleSpec = secmod_mkModuleSpec(module);
        retString = (*func)(SECMOD_MODULE_DB_FUNCTION_DEL,
                            module->parent->libraryParams, moduleSpec);
        PORT_Free(moduleSpec);
        if (retString != NULL)
            return SECSuccess;
    }
    return SECFailure;
}

SECStatus
SECMOD_FreeModuleSpecList(SECMODModule *module, char **moduleSpecList)
{
    SECMODModuleDBFunc func = (SECMODModuleDBFunc)module->moduleDBFunc;
    char **retString;
    if (func) {
        retString = (*func)(SECMOD_MODULE_DB_FUNCTION_RELEASE,
                            module->libraryParams, moduleSpecList);
        if (retString != NULL)
            return SECSuccess;
    }
    return SECFailure;
}

/*
 * load a PKCS#11 module but do not add it to the default NSS trust domain
 */
SECMODModule *
SECMOD_LoadModule(char *modulespec, SECMODModule *parent, PRBool recurse)
{
    char *library = NULL, *moduleName = NULL, *parameters = NULL, *nss = NULL;
    char *config = NULL;
    SECStatus status;
    SECMODModule *module = NULL;
    SECMODModule *oldModule = NULL;
    SECStatus rv;
    PRBool forwardPolicyFeedback = PR_FALSE;
    PRUint32 forwardPolicyCheckFlags;

    /* initialize the underlying module structures */
    SECMOD_Init();

    status = NSSUTIL_ArgParseModuleSpecEx(modulespec, &library, &moduleName,
                                          &parameters, &nss,
                                          &config);
    if (status != SECSuccess) {
        goto loser;
    }

    module = SECMOD_CreateModuleEx(library, moduleName, parameters, nss, config);
    forwardPolicyFeedback = NSSUTIL_ArgHasFlag("flags", "printPolicyFeedback", nss);
    forwardPolicyCheckFlags = secmod_parsePolicyCheckFlags(nss);

    if (library)
        PORT_Free(library);
    if (moduleName)
        PORT_Free(moduleName);
    if (parameters)
        PORT_Free(parameters);
    if (nss)
        PORT_Free(nss);
    if (config)
        PORT_Free(config);
    if (!module) {
        goto loser;
    }

    /* a policy only stanza doesn't actually get 'loaded'. policy has already
     * been parsed as a side effect of the CreateModuleEx call */
    if (secmod_PolicyOnly(module)) {
        return module;
    }
    if (parent) {
        module->parent = SECMOD_ReferenceModule(parent);
        if (module->internal && secmod_IsInternalKeySlot(parent)) {
            module->internal = parent->internal;
        }
    }

    /* load it */
    rv = secmod_LoadPKCS11Module(module, &oldModule);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* if we just reload an old module, no need to add it to any lists.
     * we simple release all our references */
    if (oldModule) {
        /* This module already exists, don't link it anywhere. This
         * will probably destroy this module */
        SECMOD_DestroyModule(module);
        return oldModule;
    }

    if (recurse && module->isModuleDB) {
        char **moduleSpecList;
        PORT_SetError(0);

        moduleSpecList = SECMOD_GetModuleSpecList(module);
        if (moduleSpecList) {
            char **index;

            index = moduleSpecList;
            if (*index && SECMOD_GetSkipFirstFlag(module)) {
                index++;
            }

            for (; *index; index++) {
                SECMODModule *child;
                if (0 == PORT_Strcmp(*index, modulespec)) {
                    /* avoid trivial infinite recursion */
                    PORT_SetError(SEC_ERROR_NO_MODULE);
                    rv = SECFailure;
                    break;
                }
                if (!forwardPolicyFeedback) {
                    child = SECMOD_LoadModule(*index, module, PR_TRUE);
                } else {
                    /* Add printPolicyFeedback to the nss flags */
                    char *specWithForwards =
                        NSSUTIL_AddNSSFlagToModuleSpec(*index, "printPolicyFeedback");
                    char *tmp;
                    if (forwardPolicyCheckFlags & SECMOD_FLAG_POLICY_CHECK_IDENTIFIER) {
                        tmp = NSSUTIL_AddNSSFlagToModuleSpec(specWithForwards, "policyCheckIdentifier");
                        PORT_Free(specWithForwards);
                        specWithForwards = tmp;
                    }
                    if (forwardPolicyCheckFlags & SECMOD_FLAG_POLICY_CHECK_VALUE) {
                        tmp = NSSUTIL_AddNSSFlagToModuleSpec(specWithForwards, "policyCheckValue");
                        PORT_Free(specWithForwards);
                        specWithForwards = tmp;
                    }
                    child = SECMOD_LoadModule(specWithForwards, module, PR_TRUE);
                    PORT_Free(specWithForwards);
                }
                if (!child)
                    break;
                if (child->isCritical && !child->loaded) {
                    int err = PORT_GetError();
                    if (!err)
                        err = SEC_ERROR_NO_MODULE;
                    SECMOD_DestroyModule(child);
                    PORT_SetError(err);
                    rv = SECFailure;
                    break;
                }
                SECMOD_DestroyModule(child);
            }
            SECMOD_FreeModuleSpecList(module, moduleSpecList);
        } else {
            if (!PORT_GetError())
                PORT_SetError(SEC_ERROR_NO_MODULE);
            rv = SECFailure;
        }
    }

    if (rv != SECSuccess) {
        goto loser;
    }

    /* inherit the reference */
    if (!module->moduleDBOnly) {
        SECMOD_AddModuleToList(module);
    } else {
        SECMOD_AddModuleToDBOnlyList(module);
    }

    /* handle any additional work here */
    return module;

loser:
    if (module) {
        if (module->loaded) {
            SECMOD_UnloadModule(module);
        }
        SECMOD_AddModuleToUnloadList(module);
    }
    return module;
}

SECMODModule *
SECMOD_LoadModuleWithFunction(const char *moduleName, CK_C_GetFunctionList fentry)
{
    SECMODModule *module = NULL;
    SECMODModule *oldModule = NULL;
    SECStatus rv;

    /* initialize the underlying module structures */
    SECMOD_Init();

    module = secmod_NewModule();
    if (module == NULL) {
        goto loser;
    }

    module->commonName = PORT_ArenaStrdup(module->arena, moduleName ? moduleName : "");
    module->internal = PR_FALSE;
    module->isFIPS = PR_FALSE;
    /* if the system FIPS mode is enabled, force FIPS to be on */
    if (SECMOD_GetSystemFIPSEnabled()) {
        module->isFIPS = PR_TRUE;
    }

    module->isCritical = PR_FALSE;
    /* new field */
    module->trustOrder = NSSUTIL_DEFAULT_TRUST_ORDER;
    /* new field */
    module->cipherOrder = NSSUTIL_DEFAULT_CIPHER_ORDER;
    /* new field */
    module->isModuleDB = PR_FALSE;
    module->moduleDBOnly = PR_FALSE;

    module->ssl[0] = 0;
    module->ssl[1] = 0;

    secmod_PrivateModuleCount++;

    /* load it */
    rv = secmod_LoadPKCS11ModuleFromFunction(module, &oldModule, fentry);
    if (rv != SECSuccess) {
        goto loser;
    }

    /* if we just reload an old module, no need to add it to any lists.
     * we simple release all our references */
    if (oldModule) {
        /* This module already exists, don't link it anywhere. This
         * will probably destroy this module */
        SECMOD_DestroyModule(module);
        return oldModule;
    }

    SECMOD_AddModuleToList(module);
    /* handle any additional work here */
    return module;

loser:
    if (module) {
        if (module->loaded) {
            SECMOD_UnloadModule(module);
        }
        SECMOD_AddModuleToUnloadList(module);
    }
    return module;
}

/*
 * load a PKCS#11 module and add it to the default NSS trust domain
 */
SECMODModule *
SECMOD_LoadUserModule(char *modulespec, SECMODModule *parent, PRBool recurse)
{
    SECStatus rv = SECSuccess;
    SECMODModule *newmod = SECMOD_LoadModule(modulespec, parent, recurse);
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();

    if (newmod) {
        SECMOD_GetReadLock(moduleLock);
        rv = STAN_AddModuleToDefaultTrustDomain(newmod);
        SECMOD_ReleaseReadLock(moduleLock);
        if (SECSuccess != rv) {
            SECMOD_DestroyModule(newmod);
            return NULL;
        }
    }
    return newmod;
}

SECMODModule *
SECMOD_LoadUserModuleWithFunction(const char *moduleName, CK_C_GetFunctionList fentry)
{
    SECStatus rv = SECSuccess;
    SECMODModule *newmod = SECMOD_LoadModuleWithFunction(moduleName, fentry);
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();

    if (newmod) {
        SECMOD_GetReadLock(moduleLock);
        rv = STAN_AddModuleToDefaultTrustDomain(newmod);
        SECMOD_ReleaseReadLock(moduleLock);
        if (SECSuccess != rv) {
            SECMOD_DestroyModule(newmod);
            return NULL;
        }
    }
    return newmod;
}

/*
 * remove the PKCS#11 module from the default NSS trust domain, call
 * C_Finalize, and destroy the module structure
 */
SECStatus
SECMOD_UnloadUserModule(SECMODModule *mod)
{
    SECStatus rv = SECSuccess;
    int atype = 0;
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();
    if (!mod) {
        return SECFailure;
    }

    SECMOD_GetReadLock(moduleLock);
    rv = STAN_RemoveModuleFromDefaultTrustDomain(mod);
    SECMOD_ReleaseReadLock(moduleLock);
    if (SECSuccess != rv) {
        return SECFailure;
    }
    return SECMOD_DeleteModuleEx(NULL, mod, &atype, PR_FALSE);
}
