/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * This file maps various PKCS #11 Mechanisms to related mechanisms, key
 * types, and ASN.1 encodings.
 */
#include "seccomon.h"
#include "secmod.h"
#include "secmodi.h"
#include "pkcs11t.h"
#include "pk11func.h"
#include "secitem.h"
#include "secder.h"
#include "secasn1.h"
#include "secoid.h"
#include "secerr.h"

/*************************************************************
 * local static and global data
 *************************************************************/

/*
 * Tables used for Extended mechanism mapping (currently not used)
 */
typedef struct {
    CK_MECHANISM_TYPE keyGen;
    CK_KEY_TYPE keyType;
    CK_MECHANISM_TYPE type;
    CK_MECHANISM_TYPE padType;
    int blockSize;
    int iv;
} pk11MechanismData;

static pk11MechanismData pk11_default = { CKM_GENERIC_SECRET_KEY_GEN, CKK_GENERIC_SECRET,
                                          CKM_FAKE_RANDOM, CKM_FAKE_RANDOM, 8, 8 };
static pk11MechanismData *pk11_MechanismTable = NULL;
static int pk11_MechTableSize = 0;
static int pk11_MechEntrySize = 0;

/*
 * list of mechanisms we're willing to wrap secret keys with.
 * This list is ordered by preference.
 */
CK_MECHANISM_TYPE wrapMechanismList[] = {
    CKM_DES3_ECB,
    CKM_CAST5_ECB,
    CKM_AES_ECB,
    CKM_CAMELLIA_ECB,
    CKM_SEED_ECB,
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
};

int wrapMechanismCount = sizeof(wrapMechanismList) / sizeof(wrapMechanismList[0]);

/*********************************************************************
 *       Mechanism Mapping functions
 *********************************************************************/

/*
 * lookup an entry in the mechanism table. If none found, return the
 * default structure.
 */
static pk11MechanismData *
pk11_lookup(CK_MECHANISM_TYPE type)
{
    int i;
    for (i = 0; i < pk11_MechEntrySize; i++) {
        if (pk11_MechanismTable[i].type == type) {
            return (&pk11_MechanismTable[i]);
        }
    }
    return &pk11_default;
}

/*
 * find the best key wrap mechanism for this slot.
 */
CK_MECHANISM_TYPE
PK11_GetBestWrapMechanism(PK11SlotInfo *slot)
{
    int i;
    for (i = 0; i < wrapMechanismCount; i++) {
        if (PK11_DoesMechanism(slot, wrapMechanismList[i])) {
            return wrapMechanismList[i];
        }
    }
    return CKM_INVALID_MECHANISM;
}

/*
 * NOTE: This is not thread safe. Called at init time, and when loading
 * a new Entry. It is reasonably safe as long as it is not re-entered
 * (readers will always see a consistant table)
 *
 * This routine is called to add entries to the mechanism table, once there,
 * they can not be removed.
 */
void
PK11_AddMechanismEntry(CK_MECHANISM_TYPE type, CK_KEY_TYPE key,
                       CK_MECHANISM_TYPE keyGen,
                       CK_MECHANISM_TYPE padType,
                       int ivLen, int blockSize)
{
    int tableSize = pk11_MechTableSize;
    int size = pk11_MechEntrySize;
    int entry = size++;
    pk11MechanismData *old = pk11_MechanismTable;
    pk11MechanismData *newt = pk11_MechanismTable;

    if (size > tableSize) {
        int oldTableSize = tableSize;
        tableSize += 10;
        newt = PORT_NewArray(pk11MechanismData, tableSize);
        if (newt == NULL)
            return;

        if (old)
            PORT_Memcpy(newt, old, oldTableSize * sizeof(*newt));
    } else
        old = NULL;

    newt[entry].type = type;
    newt[entry].keyType = key;
    newt[entry].keyGen = keyGen;
    newt[entry].padType = padType;
    newt[entry].iv = ivLen;
    newt[entry].blockSize = blockSize;

    pk11_MechanismTable = newt;
    pk11_MechTableSize = tableSize;
    pk11_MechEntrySize = size;
    if (old)
        PORT_Free(old);
}

/*
 * Get the mechanism needed for the given key type
 */
CK_MECHANISM_TYPE
PK11_GetKeyMechanism(CK_KEY_TYPE type)
{
    switch (type) {
        case CKK_SEED:
            return CKM_SEED_CBC;
        case CKK_CAMELLIA:
            return CKM_CAMELLIA_CBC;
        case CKK_NSS_CHACHA20:
            return CKM_NSS_CHACHA20_POLY1305;
        case CKK_CHACHA20:
            return CKM_CHACHA20_POLY1305;
        case CKK_AES:
            return CKM_AES_CBC;
        case CKK_DES:
            return CKM_DES_CBC;
        case CKK_DES3:
            return CKM_DES3_KEY_GEN;
        case CKK_DES2:
            return CKM_DES2_KEY_GEN;
        case CKK_CDMF:
            return CKM_CDMF_CBC;
        case CKK_RC2:
            return CKM_RC2_CBC;
        case CKK_RC4:
            return CKM_RC4;
        case CKK_RC5:
            return CKM_RC5_CBC;
        case CKK_SKIPJACK:
            return CKM_SKIPJACK_CBC64;
        case CKK_BATON:
            return CKM_BATON_CBC128;
        case CKK_JUNIPER:
            return CKM_JUNIPER_CBC128;
        case CKK_IDEA:
            return CKM_IDEA_CBC;
        case CKK_CAST:
            return CKM_CAST_CBC;
        case CKK_CAST3:
            return CKM_CAST3_CBC;
        case CKK_CAST5:
            return CKM_CAST5_CBC;
        case CKK_RSA:
            return CKM_RSA_PKCS;
        case CKK_DSA:
            return CKM_DSA;
        case CKK_DH:
            return CKM_DH_PKCS_DERIVE;
        case CKK_KEA:
            return CKM_KEA_KEY_DERIVE;
        case CKK_EC: /* CKK_ECDSA is deprecated */
            return CKM_ECDSA;
        case CKK_EC_EDWARDS:
            return CKM_EDDSA;
        case CKK_HKDF:
            return CKM_HKDF_DERIVE;
        case CKK_ML_DSA:
            return CKM_ML_DSA;
        case CKK_GENERIC_SECRET:
        default:
            return CKM_SHA_1_HMAC;
    }
}

/*
 * Get the key type needed for the given mechanism
 */
CK_KEY_TYPE
PK11_GetKeyType(CK_MECHANISM_TYPE type, unsigned long len)
{
    switch (type) {
        case CKM_SEED_ECB:
        case CKM_SEED_CBC:
        case CKM_SEED_MAC:
        case CKM_SEED_MAC_GENERAL:
        case CKM_SEED_CBC_PAD:
        case CKM_SEED_KEY_GEN:
            return CKK_SEED;
        case CKM_CAMELLIA_ECB:
        case CKM_CAMELLIA_CBC:
        case CKM_CAMELLIA_MAC:
        case CKM_CAMELLIA_MAC_GENERAL:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_CAMELLIA_KEY_GEN:
            return CKK_CAMELLIA;
        case CKM_NSS_CHACHA20_POLY1305:
        case CKM_NSS_CHACHA20_KEY_GEN:
        case CKM_NSS_CHACHA20_CTR:
            return CKK_NSS_CHACHA20;
        case CKM_CHACHA20_POLY1305:
        case CKM_CHACHA20_KEY_GEN:
        case CKM_CHACHA20:
            return CKK_CHACHA20;
        case CKM_AES_ECB:
        case CKM_AES_CBC:
        case CKM_AES_CCM:
        case CKM_AES_CTR:
        case CKM_AES_CTS:
        case CKM_AES_GCM:
        case CKM_AES_MAC:
        case CKM_AES_MAC_GENERAL:
        case CKM_AES_CMAC:
        case CKM_AES_CMAC_GENERAL:
        case CKM_AES_CBC_PAD:
        case CKM_AES_KEY_GEN:
        case CKM_NSS_AES_KEY_WRAP:
        case CKM_NSS_AES_KEY_WRAP_PAD:
        case CKM_AES_KEY_WRAP:
        case CKM_AES_KEY_WRAP_KWP:
        case CKM_AES_XCBC_MAC:
        case CKM_AES_XCBC_MAC_96:
            return CKK_AES;
        case CKM_DES_ECB:
        case CKM_DES_CBC:
        case CKM_DES_MAC:
        case CKM_DES_MAC_GENERAL:
        case CKM_DES_CBC_PAD:
        case CKM_DES_KEY_GEN:
        case CKM_KEY_WRAP_LYNKS:
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
            return CKK_DES;
        case CKM_DES3_ECB:
        case CKM_DES3_CBC:
        case CKM_DES3_MAC:
        case CKM_DES3_MAC_GENERAL:
        case CKM_DES3_CBC_PAD:
            return (len == 16) ? CKK_DES2 : CKK_DES3;
        case CKM_DES2_KEY_GEN:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
            return CKK_DES2;
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_DES3_KEY_GEN:
            return CKK_DES3;
        case CKM_CDMF_ECB:
        case CKM_CDMF_CBC:
        case CKM_CDMF_MAC:
        case CKM_CDMF_MAC_GENERAL:
        case CKM_CDMF_CBC_PAD:
        case CKM_CDMF_KEY_GEN:
            return CKK_CDMF;
        case CKM_RC2_ECB:
        case CKM_RC2_CBC:
        case CKM_RC2_MAC:
        case CKM_RC2_MAC_GENERAL:
        case CKM_RC2_CBC_PAD:
        case CKM_RC2_KEY_GEN:
        case CKM_PBE_SHA1_RC2_128_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
            return CKK_RC2;
        case CKM_RC4:
        case CKM_RC4_KEY_GEN:
            return CKK_RC4;
        case CKM_RC5_ECB:
        case CKM_RC5_CBC:
        case CKM_RC5_MAC:
        case CKM_RC5_MAC_GENERAL:
        case CKM_RC5_CBC_PAD:
        case CKM_RC5_KEY_GEN:
            return CKK_RC5;
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_SKIPJACK_KEY_GEN:
        case CKM_SKIPJACK_WRAP:
        case CKM_SKIPJACK_PRIVATE_WRAP:
            return CKK_SKIPJACK;
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_BATON_WRAP:
        case CKM_BATON_KEY_GEN:
            return CKK_BATON;
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
        case CKM_JUNIPER_WRAP:
        case CKM_JUNIPER_KEY_GEN:
            return CKK_JUNIPER;
        case CKM_IDEA_CBC:
        case CKM_IDEA_ECB:
        case CKM_IDEA_MAC:
        case CKM_IDEA_MAC_GENERAL:
        case CKM_IDEA_CBC_PAD:
        case CKM_IDEA_KEY_GEN:
            return CKK_IDEA;
        case CKM_CAST_ECB:
        case CKM_CAST_CBC:
        case CKM_CAST_MAC:
        case CKM_CAST_MAC_GENERAL:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST_KEY_GEN:
        case CKM_PBE_MD5_CAST_CBC:
            return CKK_CAST;
        case CKM_CAST3_ECB:
        case CKM_CAST3_CBC:
        case CKM_CAST3_MAC:
        case CKM_CAST3_MAC_GENERAL:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST3_KEY_GEN:
        case CKM_PBE_MD5_CAST3_CBC:
            return CKK_CAST3;
        case CKM_CAST5_ECB:
        case CKM_CAST5_CBC:
        case CKM_CAST5_MAC:
        case CKM_CAST5_MAC_GENERAL:
        case CKM_CAST5_CBC_PAD:
        case CKM_CAST5_KEY_GEN:
        case CKM_PBE_MD5_CAST5_CBC:
            return CKK_CAST5;
        case CKM_RSA_PKCS:
        case CKM_RSA_9796:
        case CKM_RSA_X_509:
        case CKM_MD2_RSA_PKCS:
        case CKM_MD5_RSA_PKCS:
        case CKM_SHA1_RSA_PKCS:
        case CKM_SHA224_RSA_PKCS:
        case CKM_SHA256_RSA_PKCS:
        case CKM_SHA384_RSA_PKCS:
        case CKM_SHA512_RSA_PKCS:
        case CKM_KEY_WRAP_SET_OAEP:
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
        case CKM_RSA_X9_31_KEY_PAIR_GEN:
            return CKK_RSA;
        case CKM_DSA:
        case CKM_DSA_SHA1:
        case CKM_DSA_KEY_PAIR_GEN:
            return CKK_DSA;
        case CKM_DH_PKCS_DERIVE:
        case CKM_DH_PKCS_KEY_PAIR_GEN:
            return CKK_DH;
        case CKM_KEA_KEY_DERIVE:
        case CKM_KEA_KEY_PAIR_GEN:
            return CKK_KEA;
        case CKM_ECDSA:
        case CKM_ECDSA_SHA1:
        case CKM_EC_KEY_PAIR_GEN: /* aka CKM_ECDSA_KEY_PAIR_GEN */
        case CKM_ECDH1_DERIVE:
            return CKK_EC; /* CKK_ECDSA is deprecated */
        case CKM_EC_EDWARDS_KEY_PAIR_GEN:
        case CKM_EDDSA:
            return CKK_EC_EDWARDS;
        case CKM_HKDF_KEY_GEN:
        case CKM_HKDF_DERIVE:
        case CKM_HKDF_DATA:
            return CKK_HKDF;
        case CKM_SSL3_PRE_MASTER_KEY_GEN:
        case CKM_GENERIC_SECRET_KEY_GEN:
        case CKM_SSL3_MASTER_KEY_DERIVE:
        case CKM_SSL3_MASTER_KEY_DERIVE_DH:
        case CKM_SSL3_KEY_AND_MAC_DERIVE:
        case CKM_SSL3_SHA1_MAC:
        case CKM_SSL3_MD5_MAC:
        case CKM_TLS_MASTER_KEY_DERIVE:
        case CKM_NSS_TLS_MASTER_KEY_DERIVE_SHA256:
        case CKM_TLS_MASTER_KEY_DERIVE_DH:
        case CKM_NSS_TLS_MASTER_KEY_DERIVE_DH_SHA256:
        case CKM_TLS_KEY_AND_MAC_DERIVE:
        case CKM_NSS_TLS_KEY_AND_MAC_DERIVE_SHA256:
        case CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE:
        case CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_DH:
        case CKM_SHA_1_HMAC:
        case CKM_SHA_1_HMAC_GENERAL:
        case CKM_SHA224_HMAC:
        case CKM_SHA224_HMAC_GENERAL:
        case CKM_SHA256_HMAC:
        case CKM_SHA256_HMAC_GENERAL:
        case CKM_SHA384_HMAC:
        case CKM_SHA384_HMAC_GENERAL:
        case CKM_SHA512_HMAC:
        case CKM_SHA512_HMAC_GENERAL:
        case CKM_MD2_HMAC:
        case CKM_MD2_HMAC_GENERAL:
        case CKM_MD5_HMAC:
        case CKM_MD5_HMAC_GENERAL:
        case CKM_TLS_PRF_GENERAL:
        case CKM_NSS_TLS_PRF_GENERAL_SHA256:
            return CKK_GENERIC_SECRET;
        case CKM_NSS_KYBER_KEY_PAIR_GEN:
            return CKK_NSS_KYBER;
        case CKM_NSS_ML_KEM_KEY_PAIR_GEN:
            return CKK_NSS_ML_KEM;
        case CKM_ML_DSA_KEY_PAIR_GEN:
        case CKM_ML_DSA:
            return CKK_ML_DSA;
        default:
            return pk11_lookup(type)->keyType;
    }
}

/*
 * Get the Key Gen Mechanism needed for the given
 * crypto mechanism
 */
CK_MECHANISM_TYPE
PK11_GetKeyGen(CK_MECHANISM_TYPE type)
{
    return PK11_GetKeyGenWithSize(type, 0);
}

CK_MECHANISM_TYPE
PK11_GetKeyGenWithSize(CK_MECHANISM_TYPE type, int size)
{
    switch (type) {
        case CKM_SEED_ECB:
        case CKM_SEED_CBC:
        case CKM_SEED_MAC:
        case CKM_SEED_MAC_GENERAL:
        case CKM_SEED_CBC_PAD:
        case CKM_SEED_KEY_GEN:
            return CKM_SEED_KEY_GEN;
        case CKM_CAMELLIA_ECB:
        case CKM_CAMELLIA_CBC:
        case CKM_CAMELLIA_MAC:
        case CKM_CAMELLIA_MAC_GENERAL:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_CAMELLIA_KEY_GEN:
            return CKM_CAMELLIA_KEY_GEN;
        case CKM_NSS_CHACHA20_POLY1305:
        case CKM_NSS_CHACHA20_CTR:
            return CKM_NSS_CHACHA20_KEY_GEN;
        case CKM_CHACHA20_POLY1305:
        case CKM_CHACHA20:
            return CKM_CHACHA20_KEY_GEN;
        case CKM_AES_ECB:
        case CKM_AES_CBC:
        case CKM_AES_CCM:
        case CKM_AES_CTR:
        case CKM_AES_CTS:
        case CKM_AES_GCM:
        case CKM_AES_MAC:
        case CKM_AES_MAC_GENERAL:
        case CKM_AES_CMAC:
        case CKM_AES_CMAC_GENERAL:
        case CKM_AES_CBC_PAD:
        case CKM_AES_KEY_GEN:
            return CKM_AES_KEY_GEN;
        case CKM_DES_ECB:
        case CKM_DES_CBC:
        case CKM_DES_MAC:
        case CKM_DES_MAC_GENERAL:
        case CKM_KEY_WRAP_LYNKS:
        case CKM_DES_CBC_PAD:
        case CKM_DES_KEY_GEN:
            return CKM_DES_KEY_GEN;
        case CKM_DES3_ECB:
        case CKM_DES3_CBC:
        case CKM_DES3_MAC:
        case CKM_DES3_MAC_GENERAL:
        case CKM_DES3_CBC_PAD:
            return (size == 16) ? CKM_DES2_KEY_GEN : CKM_DES3_KEY_GEN;
        case CKM_DES3_KEY_GEN:
            return CKM_DES3_KEY_GEN;
        case CKM_DES2_KEY_GEN:
            return CKM_DES2_KEY_GEN;
        case CKM_CDMF_ECB:
        case CKM_CDMF_CBC:
        case CKM_CDMF_MAC:
        case CKM_CDMF_MAC_GENERAL:
        case CKM_CDMF_CBC_PAD:
        case CKM_CDMF_KEY_GEN:
            return CKM_CDMF_KEY_GEN;
        case CKM_RC2_ECB:
        case CKM_RC2_CBC:
        case CKM_RC2_MAC:
        case CKM_RC2_MAC_GENERAL:
        case CKM_RC2_CBC_PAD:
        case CKM_RC2_KEY_GEN:
            return CKM_RC2_KEY_GEN;
        case CKM_RC4:
        case CKM_RC4_KEY_GEN:
            return CKM_RC4_KEY_GEN;
        case CKM_RC5_ECB:
        case CKM_RC5_CBC:
        case CKM_RC5_MAC:
        case CKM_RC5_MAC_GENERAL:
        case CKM_RC5_CBC_PAD:
        case CKM_RC5_KEY_GEN:
            return CKM_RC5_KEY_GEN;
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_SKIPJACK_WRAP:
        case CKM_SKIPJACK_KEY_GEN:
            return CKM_SKIPJACK_KEY_GEN;
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_BATON_WRAP:
        case CKM_BATON_KEY_GEN:
            return CKM_BATON_KEY_GEN;
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
        case CKM_JUNIPER_WRAP:
        case CKM_JUNIPER_KEY_GEN:
            return CKM_JUNIPER_KEY_GEN;
        case CKM_IDEA_CBC:
        case CKM_IDEA_ECB:
        case CKM_IDEA_MAC:
        case CKM_IDEA_MAC_GENERAL:
        case CKM_IDEA_CBC_PAD:
        case CKM_IDEA_KEY_GEN:
            return CKM_IDEA_KEY_GEN;
        case CKM_CAST_ECB:
        case CKM_CAST_CBC:
        case CKM_CAST_MAC:
        case CKM_CAST_MAC_GENERAL:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST_KEY_GEN:
            return CKM_CAST_KEY_GEN;
        case CKM_CAST3_ECB:
        case CKM_CAST3_CBC:
        case CKM_CAST3_MAC:
        case CKM_CAST3_MAC_GENERAL:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST3_KEY_GEN:
            return CKM_CAST3_KEY_GEN;
        case CKM_CAST5_ECB:
        case CKM_CAST5_CBC:
        case CKM_CAST5_MAC:
        case CKM_CAST5_MAC_GENERAL:
        case CKM_CAST5_CBC_PAD:
        case CKM_CAST5_KEY_GEN:
            return CKM_CAST5_KEY_GEN;
        case CKM_RSA_PKCS:
        case CKM_RSA_9796:
        case CKM_RSA_X_509:
        case CKM_MD2_RSA_PKCS:
        case CKM_MD5_RSA_PKCS:
        case CKM_SHA1_RSA_PKCS:
        case CKM_SHA224_RSA_PKCS:
        case CKM_SHA256_RSA_PKCS:
        case CKM_SHA384_RSA_PKCS:
        case CKM_SHA512_RSA_PKCS:
        case CKM_KEY_WRAP_SET_OAEP:
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
            return CKM_RSA_PKCS_KEY_PAIR_GEN;
        case CKM_RSA_X9_31_KEY_PAIR_GEN:
            return CKM_RSA_X9_31_KEY_PAIR_GEN;
        case CKM_DSA:
        case CKM_DSA_SHA1:
        case CKM_DSA_KEY_PAIR_GEN:
            return CKM_DSA_KEY_PAIR_GEN;
        case CKM_DH_PKCS_DERIVE:
        case CKM_DH_PKCS_KEY_PAIR_GEN:
            return CKM_DH_PKCS_KEY_PAIR_GEN;
        case CKM_KEA_KEY_DERIVE:
        case CKM_KEA_KEY_PAIR_GEN:
            return CKM_KEA_KEY_PAIR_GEN;
        case CKM_ECDSA:
        case CKM_ECDSA_SHA1:
        case CKM_EC_KEY_PAIR_GEN: /* aka CKM_ECDSA_KEY_PAIR_GEN */
        case CKM_ECDH1_DERIVE:
            return CKM_EC_KEY_PAIR_GEN;
        case CKM_EDDSA:
            return CKM_EC_EDWARDS_KEY_PAIR_GEN;
        case CKM_SSL3_PRE_MASTER_KEY_GEN:
        case CKM_SSL3_MASTER_KEY_DERIVE:
        case CKM_SSL3_KEY_AND_MAC_DERIVE:
        case CKM_SSL3_SHA1_MAC:
        case CKM_SSL3_MD5_MAC:
        case CKM_TLS_MASTER_KEY_DERIVE:
        case CKM_TLS_KEY_AND_MAC_DERIVE:
        case CKM_NSS_TLS_KEY_AND_MAC_DERIVE_SHA256:
        case CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE:
        case CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_DH:
            return CKM_SSL3_PRE_MASTER_KEY_GEN;
        case CKM_SHA_1_HMAC:
        case CKM_SHA_1_HMAC_GENERAL:
        case CKM_SHA224_HMAC:
        case CKM_SHA224_HMAC_GENERAL:
        case CKM_SHA256_HMAC:
        case CKM_SHA256_HMAC_GENERAL:
        case CKM_SHA384_HMAC:
        case CKM_SHA384_HMAC_GENERAL:
        case CKM_SHA512_HMAC:
        case CKM_SHA512_HMAC_GENERAL:
        case CKM_MD2_HMAC:
        case CKM_MD2_HMAC_GENERAL:
        case CKM_MD5_HMAC:
        case CKM_MD5_HMAC_GENERAL:
        case CKM_TLS_PRF_GENERAL:
        case CKM_NSS_TLS_PRF_GENERAL_SHA256:
        case CKM_GENERIC_SECRET_KEY_GEN:
            return CKM_GENERIC_SECRET_KEY_GEN;
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
        case CKM_PBA_SHA1_WITH_SHA1_HMAC:
        case CKM_NSS_PBE_SHA1_HMAC_KEY_GEN:
        case CKM_NSS_PBE_MD5_HMAC_KEY_GEN:
        case CKM_NSS_PBE_MD2_HMAC_KEY_GEN:
        case CKM_NSS_PKCS12_PBE_SHA224_HMAC_KEY_GEN:
        case CKM_NSS_PKCS12_PBE_SHA256_HMAC_KEY_GEN:
        case CKM_NSS_PKCS12_PBE_SHA384_HMAC_KEY_GEN:
        case CKM_NSS_PKCS12_PBE_SHA512_HMAC_KEY_GEN:
        case CKM_NSS_PBE_SHA1_DES_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_128_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC4:
        case CKM_NSS_PBE_SHA1_128_BIT_RC4:
        case CKM_NSS_PBE_SHA1_TRIPLE_DES_CBC:
        case CKM_NSS_PBE_SHA1_FAULTY_3DES_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
        case CKM_PBE_SHA1_RC2_128_CBC:
        case CKM_PBE_SHA1_RC4_40:
        case CKM_PBE_SHA1_RC4_128:
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
        case CKM_PKCS5_PBKD2:
            return type;
        case CKM_ML_DSA:
            return CKM_ML_DSA_KEY_PAIR_GEN;
        default:
            return pk11_lookup(type)->keyGen;
    }
}

/*
 * get the mechanism block size
 */
int
PK11_GetBlockSize(CK_MECHANISM_TYPE type, SECItem *params)
{
    CK_RC5_PARAMS *rc5_params;
    CK_RC5_CBC_PARAMS *rc5_cbc_params;
    switch (type) {
        case CKM_RC5_ECB:
            if ((params) && (params->data)) {
                rc5_params = (CK_RC5_PARAMS *)params->data;
                return (rc5_params->ulWordsize) * 2;
            }
            return 8;
        case CKM_RC5_CBC:
        case CKM_RC5_CBC_PAD:
            if ((params) && (params->data)) {
                rc5_cbc_params = (CK_RC5_CBC_PARAMS *)params->data;
                return (rc5_cbc_params->ulWordsize) * 2;
            }
            return 8;
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_RC2_ECB:
        case CKM_IDEA_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
        case CKM_RC2_CBC:
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_RC2_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
        case CKM_NSS_PBE_SHA1_DES_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_128_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_TRIPLE_DES_CBC:
        case CKM_NSS_PBE_SHA1_FAULTY_3DES_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
        case CKM_PBE_SHA1_RC2_128_CBC:
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
            return 8;
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
            return 4;
        case CKM_SEED_ECB:
        case CKM_SEED_CBC:
        case CKM_SEED_CBC_PAD:
        case CKM_CAMELLIA_ECB:
        case CKM_CAMELLIA_CBC:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_AES_ECB:
        case CKM_AES_CBC:
        case CKM_AES_CBC_PAD:
        case CKM_BATON_ECB128:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            return 16;
        case CKM_BATON_ECB96:
            return 12;
        case CKM_RC4:
        case CKM_NSS_PBE_SHA1_40_BIT_RC4:
        case CKM_NSS_PBE_SHA1_128_BIT_RC4:
        case CKM_PBE_SHA1_RC4_40:
        case CKM_PBE_SHA1_RC4_128:
            return 0;
        case CKM_RSA_PKCS:
        case CKM_RSA_9796:
        case CKM_RSA_X_509:
            /*actually it's the modulus length of the key!*/
            return -1; /* failure */
        case CKM_NSS_CHACHA20_POLY1305:
        case CKM_NSS_CHACHA20_CTR:
        case CKM_CHACHA20_POLY1305:
        case CKM_CHACHA20:
            return 64;
        default:
            return pk11_lookup(type)->blockSize;
    }
}

/*
 * get the iv length
 */
int
PK11_GetIVLength(CK_MECHANISM_TYPE type)
{
    switch (type) {
        case CKM_SEED_ECB:
        case CKM_CAMELLIA_ECB:
        case CKM_AES_ECB:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_RC2_ECB:
        case CKM_IDEA_ECB:
        case CKM_SKIPJACK_WRAP:
        case CKM_BATON_WRAP:
        case CKM_RC5_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
        case CKM_AES_KEY_WRAP:
        case CKM_AES_KEY_WRAP_PAD:
        case CKM_AES_KEY_WRAP_KWP:
        case CKM_NSS_AES_KEY_WRAP:
        case CKM_NSS_AES_KEY_WRAP_PAD:
            return 0;
        case CKM_RC2_CBC:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
        case CKM_NSS_PBE_SHA1_DES_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_128_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_TRIPLE_DES_CBC:
        case CKM_NSS_PBE_SHA1_FAULTY_3DES_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
        case CKM_PBE_SHA1_RC2_128_CBC:
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
        case CKM_RC5_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_RC2_CBC_PAD:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_RC5_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
            return 8;
        case CKM_AES_GCM:
        case CKM_NSS_CHACHA20_POLY1305:
        case CKM_CHACHA20_POLY1305:
            return 12;
        case CKM_SEED_CBC:
        case CKM_SEED_CBC_PAD:
        case CKM_CAMELLIA_CBC:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_AES_CBC:
        case CKM_AES_CBC_PAD:
        case CKM_NSS_CHACHA20_CTR:
        case CKM_CHACHA20:
            return 16;
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            return 24;
        case CKM_RC4:
        case CKM_RSA_PKCS:
        case CKM_RSA_9796:
        case CKM_RSA_X_509:
        case CKM_NSS_PBE_SHA1_40_BIT_RC4:
        case CKM_NSS_PBE_SHA1_128_BIT_RC4:
        case CKM_PBE_SHA1_RC4_40:
        case CKM_PBE_SHA1_RC4_128:
            return 0;
        default:
            return pk11_lookup(type)->iv;
    }
}

/* These next two utilities are here to help facilitate future
 * Dynamic Encrypt/Decrypt symetric key mechanisms, and to allow functions
 * like SSL and S-MIME to automatically add them.
 */
SECItem *
pk11_ParamFromIVWithLen(CK_MECHANISM_TYPE type, SECItem *iv, int keyLen)
{
    CK_RC2_CBC_PARAMS *rc2_params = NULL;
    CK_RC2_PARAMS *rc2_ecb_params = NULL;
    CK_RC5_PARAMS *rc5_params = NULL;
    CK_RC5_CBC_PARAMS *rc5_cbc_params = NULL;
    SECItem *param;

    param = (SECItem *)PORT_Alloc(sizeof(SECItem));
    if (param == NULL)
        return NULL;
    param->data = NULL;
    param->len = 0;
    param->type = 0;
    switch (type) {
        case CKM_SEED_ECB:
        case CKM_CAMELLIA_ECB:
        case CKM_AES_ECB:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_RSA_PKCS:
        case CKM_RSA_X_509:
        case CKM_RSA_9796:
        case CKM_IDEA_ECB:
        case CKM_CDMF_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
        case CKM_RC4:
        case CKM_AES_KEY_WRAP:
        case CKM_AES_KEY_WRAP_PAD:
        case CKM_AES_KEY_WRAP_KWP:
        case CKM_NSS_AES_KEY_WRAP:
        case CKM_NSS_AES_KEY_WRAP_PAD:
            break;
        case CKM_RC2_ECB:
            rc2_ecb_params = (CK_RC2_PARAMS *)PORT_Alloc(sizeof(CK_RC2_PARAMS));
            if (rc2_ecb_params == NULL)
                break;
            /*  Maybe we should pass the key size in too to get this value? */
            *rc2_ecb_params = keyLen ? keyLen * 8 : 128;
            param->data = (unsigned char *)rc2_ecb_params;
            param->len = sizeof(CK_RC2_PARAMS);
            break;
        case CKM_RC2_CBC:
        case CKM_RC2_CBC_PAD:
            rc2_params = (CK_RC2_CBC_PARAMS *)PORT_Alloc(sizeof(CK_RC2_CBC_PARAMS));
            if (rc2_params == NULL)
                break;
            /* Maybe we should pass the key size in too to get this value? */
            rc2_params->ulEffectiveBits = keyLen ? keyLen * 8 : 128;
            if (iv && iv->data)
                PORT_Memcpy(rc2_params->iv, iv->data, sizeof(rc2_params->iv));
            param->data = (unsigned char *)rc2_params;
            param->len = sizeof(CK_RC2_CBC_PARAMS);
            break;
        case CKM_RC5_CBC:
        case CKM_RC5_CBC_PAD:
            rc5_cbc_params = (CK_RC5_CBC_PARAMS *)
                PORT_Alloc(sizeof(CK_RC5_CBC_PARAMS) + ((iv) ? iv->len : 0));
            if (rc5_cbc_params == NULL)
                break;
            if (iv && iv->data && iv->len) {
                rc5_cbc_params->pIv = ((CK_BYTE_PTR)rc5_cbc_params) + sizeof(CK_RC5_CBC_PARAMS);
                PORT_Memcpy(rc5_cbc_params->pIv, iv->data, iv->len);
                rc5_cbc_params->ulIvLen = iv->len;
                rc5_cbc_params->ulWordsize = iv->len / 2;
            } else {
                rc5_cbc_params->ulWordsize = 4;
                rc5_cbc_params->pIv = NULL;
                rc5_cbc_params->ulIvLen = 0;
            }
            rc5_cbc_params->ulRounds = 16;
            param->data = (unsigned char *)rc5_cbc_params;
            param->len = sizeof(CK_RC5_CBC_PARAMS);
            break;
        case CKM_RC5_ECB:
            rc5_params = (CK_RC5_PARAMS *)PORT_Alloc(sizeof(CK_RC5_PARAMS));
            if (rc5_params == NULL)
                break;
            if (iv && iv->data && iv->len) {
                rc5_params->ulWordsize = iv->len / 2;
            } else {
                rc5_params->ulWordsize = 4;
            }
            rc5_params->ulRounds = 16;
            param->data = (unsigned char *)rc5_params;
            param->len = sizeof(CK_RC5_PARAMS);
            break;

        case CKM_SEED_CBC:
        case CKM_CAMELLIA_CBC:
        case CKM_AES_CBC:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_CDMF_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_AES_CBC_PAD:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_CDMF_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            if ((iv == NULL) || (iv->data == NULL))
                break;
            param->data = (unsigned char *)PORT_Alloc(iv->len);
            if (param->data != NULL) {
                PORT_Memcpy(param->data, iv->data, iv->len);
                param->len = iv->len;
            }
            break;
        /* unknown mechanism, pass IV in if it's there */
        default:
            if (pk11_lookup(type)->iv == 0) {
                break;
            }
            if ((iv == NULL) || (iv->data == NULL)) {
                break;
            }
            param->data = (unsigned char *)PORT_Alloc(iv->len);
            if (param->data != NULL) {
                PORT_Memcpy(param->data, iv->data, iv->len);
                param->len = iv->len;
            }
            break;
    }
    return param;
}

/* These next two utilities are here to help facilitate future
 * Dynamic Encrypt/Decrypt symetric key mechanisms, and to allow functions
 * like SSL and S-MIME to automatically add them.
 */
SECItem *
PK11_ParamFromIV(CK_MECHANISM_TYPE type, SECItem *iv)
{
    return pk11_ParamFromIVWithLen(type, iv, 0);
}

unsigned char *
PK11_IVFromParam(CK_MECHANISM_TYPE type, SECItem *param, int *len)
{
    CK_RC2_CBC_PARAMS *rc2_params;
    CK_RC5_CBC_PARAMS *rc5_cbc_params;

    *len = 0;
    switch (type) {
        case CKM_SEED_ECB:
        case CKM_CAMELLIA_ECB:
        case CKM_AES_ECB:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_RSA_PKCS:
        case CKM_RSA_X_509:
        case CKM_RSA_9796:
        case CKM_IDEA_ECB:
        case CKM_CDMF_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
        case CKM_RC4:
            return NULL;
        case CKM_RC2_ECB:
            return NULL;
        case CKM_RC2_CBC:
        case CKM_RC2_CBC_PAD:
            rc2_params = (CK_RC2_CBC_PARAMS *)param->data;
            *len = sizeof(rc2_params->iv);
            return &rc2_params->iv[0];
        case CKM_RC5_CBC:
        case CKM_RC5_CBC_PAD:
            rc5_cbc_params = (CK_RC5_CBC_PARAMS *)param->data;
            *len = rc5_cbc_params->ulIvLen;
            return rc5_cbc_params->pIv;
        case CKM_AES_GCM:
            if (param->len == sizeof(CK_GCM_PARAMS_V3)) {
                CK_GCM_PARAMS_V3 *gcm_params = (CK_GCM_PARAMS_V3 *)param->data;
                *len = gcm_params->ulIvLen;
                return gcm_params->pIv;
            }
            if (param->len == sizeof(CK_NSS_GCM_PARAMS)) {
                CK_NSS_GCM_PARAMS *gcm_params = (CK_NSS_GCM_PARAMS *)param->data;
                *len = gcm_params->ulIvLen;
                return gcm_params->pIv;
            }
            return NULL;
        case CKM_SEED_CBC:
        case CKM_CAMELLIA_CBC:
        case CKM_AES_CBC:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_CDMF_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_AES_CBC_PAD:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_CDMF_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            break;
        /* unknown mechanism, pass IV in if it's there */
        default:
            break;
    }
    if (param->data) {
        *len = param->len;
    }
    return param->data;
}

typedef struct sec_rc5cbcParameterStr {
    SECItem version;
    SECItem rounds;
    SECItem blockSizeInBits;
    SECItem iv;
} sec_rc5cbcParameter;

static const SEC_ASN1Template sec_rc5ecb_parameter_template[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(sec_rc5cbcParameter) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc5cbcParameter, version) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc5cbcParameter, rounds) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc5cbcParameter, blockSizeInBits) },
    { 0 }
};

static const SEC_ASN1Template sec_rc5cbc_parameter_template[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(sec_rc5cbcParameter) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc5cbcParameter, version) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc5cbcParameter, rounds) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc5cbcParameter, blockSizeInBits) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(sec_rc5cbcParameter, iv) },
    { 0 }
};

typedef struct sec_rc2cbcParameterStr {
    SECItem rc2ParameterVersion;
    SECItem iv;
} sec_rc2cbcParameter;

static const SEC_ASN1Template sec_rc2cbc_parameter_template[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(sec_rc2cbcParameter) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc2cbcParameter, rc2ParameterVersion) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(sec_rc2cbcParameter, iv) },
    { 0 }
};

static const SEC_ASN1Template sec_rc2ecb_parameter_template[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(sec_rc2cbcParameter) },
    { SEC_ASN1_INTEGER,
      offsetof(sec_rc2cbcParameter, rc2ParameterVersion) },
    { 0 }
};

/* S/MIME picked id values to represent differnt keysizes */
/* I do have a formula, but it ain't pretty, and it only works because you
 * can always match three points to a parabola:) */
static unsigned char
rc2_map(SECItem *version)
{
    long x;

    x = DER_GetInteger(version);

    switch (x) {
        case 58:
            return 128;
        case 120:
            return 64;
        case 160:
            return 40;
    }
    return 128;
}

static unsigned long
rc2_unmap(unsigned long x)
{
    switch (x) {
        case 128:
            return 58;
        case 64:
            return 120;
        case 40:
            return 160;
    }
    return 58;
}

/* Generate a mechaism param from a type, and iv. */
SECItem *
PK11_ParamFromAlgid(SECAlgorithmID *algid)
{
    CK_RC2_CBC_PARAMS *rc2_cbc_params = NULL;
    CK_RC2_PARAMS *rc2_ecb_params = NULL;
    CK_RC5_CBC_PARAMS *rc5_cbc_params = NULL;
    CK_RC5_PARAMS *rc5_ecb_params = NULL;
    PLArenaPool *arena = NULL;
    SECItem *mech = NULL;
    SECOidTag algtag;
    SECStatus rv;
    CK_MECHANISM_TYPE type;
    /* initialize these to prevent UMRs in the ASN1 decoder. */
    SECItem iv = { siBuffer, NULL, 0 };
    sec_rc2cbcParameter rc2 = { { siBuffer, NULL, 0 }, { siBuffer, NULL, 0 } };
    sec_rc5cbcParameter rc5 = { { siBuffer, NULL, 0 }, { siBuffer, NULL, 0 }, { siBuffer, NULL, 0 }, { siBuffer, NULL, 0 } };

    algtag = SECOID_GetAlgorithmTag(algid);
    type = PK11_AlgtagToMechanism(algtag);

    mech = PORT_New(SECItem);
    if (mech == NULL) {
        return NULL;
    }
    mech->type = siBuffer;
    mech->data = NULL;
    mech->len = 0;

    arena = PORT_NewArena(1024);
    if (!arena) {
        goto loser;
    }

    /* handle the complicated cases */
    switch (type) {
        case CKM_RC2_ECB:
            rv = SEC_ASN1DecodeItem(arena, &rc2, sec_rc2ecb_parameter_template,
                                    &(algid->parameters));
            if (rv != SECSuccess) {
                goto loser;
            }
            rc2_ecb_params = PORT_New(CK_RC2_PARAMS);
            if (rc2_ecb_params == NULL) {
                goto loser;
            }
            *rc2_ecb_params = rc2_map(&rc2.rc2ParameterVersion);
            mech->data = (unsigned char *)rc2_ecb_params;
            mech->len = sizeof *rc2_ecb_params;
            break;
        case CKM_RC2_CBC:
        case CKM_RC2_CBC_PAD:
            rv = SEC_ASN1DecodeItem(arena, &rc2, sec_rc2cbc_parameter_template,
                                    &(algid->parameters));
            if (rv != SECSuccess) {
                goto loser;
            }
            rc2_cbc_params = PORT_New(CK_RC2_CBC_PARAMS);
            if (rc2_cbc_params == NULL) {
                goto loser;
            }
            mech->data = (unsigned char *)rc2_cbc_params;
            mech->len = sizeof *rc2_cbc_params;
            rc2_cbc_params->ulEffectiveBits = rc2_map(&rc2.rc2ParameterVersion);
            if (rc2.iv.len != sizeof rc2_cbc_params->iv) {
                PORT_SetError(SEC_ERROR_INPUT_LEN);
                goto loser;
            }
            PORT_Memcpy(rc2_cbc_params->iv, rc2.iv.data, rc2.iv.len);
            break;
        case CKM_RC5_ECB:
            rv = SEC_ASN1DecodeItem(arena, &rc5, sec_rc5ecb_parameter_template,
                                    &(algid->parameters));
            if (rv != SECSuccess) {
                goto loser;
            }
            rc5_ecb_params = PORT_New(CK_RC5_PARAMS);
            if (rc5_ecb_params == NULL) {
                goto loser;
            }
            rc5_ecb_params->ulRounds = DER_GetInteger(&rc5.rounds);
            rc5_ecb_params->ulWordsize = DER_GetInteger(&rc5.blockSizeInBits) / 8;
            mech->data = (unsigned char *)rc5_ecb_params;
            mech->len = sizeof *rc5_ecb_params;
            break;
        case CKM_RC5_CBC:
        case CKM_RC5_CBC_PAD:
            rv = SEC_ASN1DecodeItem(arena, &rc5, sec_rc5cbc_parameter_template,
                                    &(algid->parameters));
            if (rv != SECSuccess) {
                goto loser;
            }
            rc5_cbc_params = (CK_RC5_CBC_PARAMS *)
                PORT_Alloc(sizeof(CK_RC5_CBC_PARAMS) + rc5.iv.len);
            if (rc5_cbc_params == NULL) {
                goto loser;
            }
            mech->data = (unsigned char *)rc5_cbc_params;
            mech->len = sizeof *rc5_cbc_params;
            rc5_cbc_params->ulRounds = DER_GetInteger(&rc5.rounds);
            rc5_cbc_params->ulWordsize = DER_GetInteger(&rc5.blockSizeInBits) / 8;
            rc5_cbc_params->pIv = ((CK_BYTE_PTR)rc5_cbc_params) + sizeof(CK_RC5_CBC_PARAMS);
            rc5_cbc_params->ulIvLen = rc5.iv.len;
            PORT_Memcpy(rc5_cbc_params->pIv, rc5.iv.data, rc5.iv.len);
            break;
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
        case CKM_NSS_PBE_SHA1_DES_CBC:
        case CKM_NSS_PBE_SHA1_TRIPLE_DES_CBC:
        case CKM_NSS_PBE_SHA1_FAULTY_3DES_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_128_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC4:
        case CKM_NSS_PBE_SHA1_128_BIT_RC4:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
        case CKM_PBE_SHA1_RC2_128_CBC:
        case CKM_PBE_SHA1_RC4_40:
        case CKM_PBE_SHA1_RC4_128:
        case CKM_PKCS5_PBKD2:
            rv = pbe_PK11AlgidToParam(algid, mech);
            if (rv != SECSuccess) {
                goto loser;
            }
            break;
        case CKM_RC4:
        case CKM_SEED_ECB:
        case CKM_CAMELLIA_ECB:
        case CKM_AES_ECB:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_IDEA_ECB:
        case CKM_CDMF_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
            break;

        default:
            if (pk11_lookup(type)->iv == 0) {
                break;
            }
        /* FALL THROUGH */
        case CKM_SEED_CBC:
        case CKM_CAMELLIA_CBC:
        case CKM_AES_CBC:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_CDMF_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_SEED_CBC_PAD:
        case CKM_CAMELLIA_CBC_PAD:
        case CKM_AES_CBC_PAD:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_CDMF_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            /* simple cases are simply octet string encoded IVs */
            rv = SEC_ASN1DecodeItem(arena, &iv,
                                    SEC_ASN1_GET(SEC_OctetStringTemplate),
                                    &(algid->parameters));
            if (rv != SECSuccess || iv.data == NULL) {
                goto loser;
            }
            /* XXX Should be some IV length sanity check here. */
            mech->data = (unsigned char *)PORT_Alloc(iv.len);
            if (mech->data == NULL) {
                goto loser;
            }
            PORT_Memcpy(mech->data, iv.data, iv.len);
            mech->len = iv.len;
            break;
    }
    PORT_FreeArena(arena, PR_FALSE);
    return mech;

loser:
    if (arena)
        PORT_FreeArena(arena, PR_FALSE);
    SECITEM_FreeItem(mech, PR_TRUE);
    return NULL;
}

/*
 * Generate an IV for the given mechanism
 */
static SECStatus
pk11_GenIV(CK_MECHANISM_TYPE type, SECItem *iv)
{
    int iv_size = PK11_GetIVLength(type);
    SECStatus rv;

    iv->len = iv_size;
    if (iv_size == 0) {
        iv->data = NULL;
        return SECSuccess;
    }

    iv->data = (unsigned char *)PORT_Alloc(iv_size);
    if (iv->data == NULL) {
        iv->len = 0;
        return SECFailure;
    }

    rv = PK11_GenerateRandom(iv->data, iv->len);
    if (rv != SECSuccess) {
        PORT_Free(iv->data);
        iv->data = NULL;
        iv->len = 0;
        return SECFailure;
    }
    return SECSuccess;
}

/*
 * create a new parameter block from the passed in MECHANISM and the
 * key. Use Netscape's S/MIME Rules for the New param block.
 */
SECItem *
pk11_GenerateNewParamWithKeyLen(CK_MECHANISM_TYPE type, int keyLen)
{
    CK_RC2_CBC_PARAMS *rc2_params;
    CK_RC2_PARAMS *rc2_ecb_params;
    SECItem *mech;
    SECItem iv;
    SECStatus rv;

    mech = (SECItem *)PORT_Alloc(sizeof(SECItem));
    if (mech == NULL)
        return NULL;

    rv = SECSuccess;
    mech->type = siBuffer;
    mech->data = NULL;
    mech->len = 0;
    switch (type) {
        case CKM_RC4:
        case CKM_SEED_ECB:
        case CKM_CAMELLIA_ECB:
        case CKM_AES_ECB:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_IDEA_ECB:
        case CKM_CDMF_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
            break;
        case CKM_RC2_ECB:
            rc2_ecb_params = (CK_RC2_PARAMS *)PORT_Alloc(sizeof(CK_RC2_PARAMS));
            if (rc2_ecb_params == NULL) {
                rv = SECFailure;
                break;
            }
            /* NOTE PK11_GetKeyLength can return -1 if the key isn't and RC2, RC5,
             *   or RC4 key. Of course that wouldn't happen here doing RC2:).*/
            *rc2_ecb_params = keyLen ? keyLen * 8 : 128;
            mech->data = (unsigned char *)rc2_ecb_params;
            mech->len = sizeof(CK_RC2_PARAMS);
            break;
        case CKM_RC2_CBC:
        case CKM_RC2_CBC_PAD:
            rv = pk11_GenIV(type, &iv);
            if (rv != SECSuccess) {
                break;
            }
            rc2_params = (CK_RC2_CBC_PARAMS *)PORT_Alloc(sizeof(CK_RC2_CBC_PARAMS));
            if (rc2_params == NULL) {
                PORT_Free(iv.data);
                rv = SECFailure;
                break;
            }
            /* NOTE PK11_GetKeyLength can return -1 if the key isn't and RC2, RC5,
             *   or RC4 key. Of course that wouldn't happen here doing RC2:).*/
            rc2_params->ulEffectiveBits = keyLen ? keyLen * 8 : 128;
            if (iv.data)
                PORT_Memcpy(rc2_params->iv, iv.data, sizeof(rc2_params->iv));
            mech->data = (unsigned char *)rc2_params;
            mech->len = sizeof(CK_RC2_CBC_PARAMS);
            PORT_Free(iv.data);
            break;
        case CKM_RC5_ECB:
            PORT_Free(mech);
            return PK11_ParamFromIV(type, NULL);
        case CKM_RC5_CBC:
        case CKM_RC5_CBC_PAD:
            rv = pk11_GenIV(type, &iv);
            if (rv != SECSuccess) {
                break;
            }
            PORT_Free(mech);
            return PK11_ParamFromIV(type, &iv);
        default:
            if (pk11_lookup(type)->iv == 0) {
                break;
            }
        case CKM_SEED_CBC:
        case CKM_CAMELLIA_CBC:
        case CKM_AES_CBC:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_CDMF_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_CDMF_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            rv = pk11_GenIV(type, &iv);
            if (rv != SECSuccess) {
                break;
            }
            mech->data = (unsigned char *)PORT_Alloc(iv.len);
            if (mech->data == NULL) {
                PORT_Free(iv.data);
                rv = SECFailure;
                break;
            }
            if (iv.len) {
                PORT_Memcpy(mech->data, iv.data, iv.len);
            }
            mech->len = iv.len;
            PORT_Free(iv.data);
            break;
    }
    if (rv != SECSuccess) {
        SECITEM_FreeItem(mech, PR_TRUE);
        return NULL;
    }
    return mech;
}

SECItem *
PK11_GenerateNewParam(CK_MECHANISM_TYPE type, PK11SymKey *key)
{
    int keyLen = key ? PK11_GetKeyLength(key) : 0;

    return pk11_GenerateNewParamWithKeyLen(type, keyLen);
}

#define RC5_V10 0x10

/* turn a PKCS #11 parameter into a DER Encoded Algorithm ID */
SECStatus
PK11_ParamToAlgid(SECOidTag algTag, SECItem *param,
                  PLArenaPool *arena, SECAlgorithmID *algid)
{
    CK_RC2_CBC_PARAMS *rc2_params;
    sec_rc2cbcParameter rc2;
    CK_RC5_CBC_PARAMS *rc5_params;
    sec_rc5cbcParameter rc5;
    CK_MECHANISM_TYPE type = PK11_AlgtagToMechanism(algTag);
    SECItem *newParams = NULL;
    SECStatus rv = SECFailure;
    unsigned long rc2version;

    switch (type) {
        case CKM_RC4:
        case CKM_SEED_ECB:
        case CKM_CAMELLIA_ECB:
        case CKM_AES_ECB:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_IDEA_ECB:
        case CKM_CDMF_ECB:
        case CKM_CAST_ECB:
        case CKM_CAST3_ECB:
        case CKM_CAST5_ECB:
            newParams = NULL;
            rv = SECSuccess;
            break;
        case CKM_RC2_ECB:
            break;
        case CKM_RC2_CBC:
        case CKM_RC2_CBC_PAD:
            rc2_params = (CK_RC2_CBC_PARAMS *)param->data;
            rc2version = rc2_unmap(rc2_params->ulEffectiveBits);
            if (SEC_ASN1EncodeUnsignedInteger(NULL, &(rc2.rc2ParameterVersion),
                                              rc2version) == NULL)
                break;
            rc2.iv.data = rc2_params->iv;
            rc2.iv.len = sizeof(rc2_params->iv);
            newParams = SEC_ASN1EncodeItem(NULL, NULL, &rc2,
                                           sec_rc2cbc_parameter_template);
            PORT_Free(rc2.rc2ParameterVersion.data);
            if (newParams == NULL)
                break;
            rv = SECSuccess;
            break;

        case CKM_RC5_ECB: /* well not really... */
            break;
        case CKM_RC5_CBC:
        case CKM_RC5_CBC_PAD:
            rc5_params = (CK_RC5_CBC_PARAMS *)param->data;
            if (SEC_ASN1EncodeUnsignedInteger(NULL, &rc5.version, RC5_V10) == NULL)
                break;
            if (SEC_ASN1EncodeUnsignedInteger(NULL, &rc5.blockSizeInBits,
                                              rc5_params->ulWordsize * 8) == NULL) {
                PORT_Free(rc5.version.data);
                break;
            }
            if (SEC_ASN1EncodeUnsignedInteger(NULL, &rc5.rounds,
                                              rc5_params->ulWordsize * 8) == NULL) {
                PORT_Free(rc5.blockSizeInBits.data);
                PORT_Free(rc5.version.data);
                break;
            }
            rc5.iv.data = rc5_params->pIv;
            rc5.iv.len = rc5_params->ulIvLen;
            newParams = SEC_ASN1EncodeItem(NULL, NULL, &rc5,
                                           sec_rc5cbc_parameter_template);
            PORT_Free(rc5.version.data);
            PORT_Free(rc5.blockSizeInBits.data);
            PORT_Free(rc5.rounds.data);
            if (newParams == NULL)
                break;
            rv = SECSuccess;
            break;
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
        case CKM_NSS_PBE_SHA1_DES_CBC:
        case CKM_NSS_PBE_SHA1_TRIPLE_DES_CBC:
        case CKM_NSS_PBE_SHA1_FAULTY_3DES_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_128_BIT_RC2_CBC:
        case CKM_NSS_PBE_SHA1_40_BIT_RC4:
        case CKM_NSS_PBE_SHA1_128_BIT_RC4:
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
        case CKM_PBE_SHA1_RC2_128_CBC:
        case CKM_PBE_SHA1_RC4_40:
        case CKM_PBE_SHA1_RC4_128:
            return PBE_PK11ParamToAlgid(algTag, param, arena, algid);
        default:
            if (pk11_lookup(type)->iv == 0) {
                rv = SECSuccess;
                newParams = NULL;
                break;
            }
        case CKM_SEED_CBC:
        case CKM_CAMELLIA_CBC:
        case CKM_AES_CBC:
        case CKM_DES_CBC:
        case CKM_DES3_CBC:
        case CKM_IDEA_CBC:
        case CKM_CDMF_CBC:
        case CKM_CAST_CBC:
        case CKM_CAST3_CBC:
        case CKM_CAST5_CBC:
        case CKM_DES_CBC_PAD:
        case CKM_DES3_CBC_PAD:
        case CKM_IDEA_CBC_PAD:
        case CKM_CDMF_CBC_PAD:
        case CKM_CAST_CBC_PAD:
        case CKM_CAST3_CBC_PAD:
        case CKM_CAST5_CBC_PAD:
        case CKM_SKIPJACK_CBC64:
        case CKM_SKIPJACK_ECB64:
        case CKM_SKIPJACK_OFB64:
        case CKM_SKIPJACK_CFB64:
        case CKM_SKIPJACK_CFB32:
        case CKM_SKIPJACK_CFB16:
        case CKM_SKIPJACK_CFB8:
        case CKM_BATON_ECB128:
        case CKM_BATON_ECB96:
        case CKM_BATON_CBC128:
        case CKM_BATON_COUNTER:
        case CKM_BATON_SHUFFLE:
        case CKM_JUNIPER_ECB128:
        case CKM_JUNIPER_CBC128:
        case CKM_JUNIPER_COUNTER:
        case CKM_JUNIPER_SHUFFLE:
            if (param && param->len > 0) {
                newParams = SEC_ASN1EncodeItem(NULL, NULL, param,
                                               SEC_ASN1_GET(SEC_OctetStringTemplate));
                if (newParams == NULL)
                    break;
            } else {
                /* if no parameters have been supplied, then use NULL params
                 * The SECOID_SetAlgorithmID encoder will encode that as no
                 * params (since params are optional) or with an explicit NULL
                 * (for some historical cases where explicit NULL is expected).
                 */
                newParams = NULL;
            }
            rv = SECSuccess;
            break;
    }

    if (rv != SECSuccess) {
        if (newParams)
            SECITEM_FreeItem(newParams, PR_TRUE);
        return rv;
    }

    rv = SECOID_SetAlgorithmID(arena, algid, algTag, newParams);
    SECITEM_FreeItem(newParams, PR_TRUE);
    return rv;
}

/* turn an OID algorithm tag into a PKCS #11 mechanism. This allows us to
 * map OID's directly into the PKCS #11 mechanism we want to call. We find
 * this mapping in our standard OID table */
CK_MECHANISM_TYPE
PK11_AlgtagToMechanism(SECOidTag algTag)
{
    SECOidData *oid = SECOID_FindOIDByTag(algTag);

    if (oid)
        return (CK_MECHANISM_TYPE)oid->mechanism;
    return CKM_INVALID_MECHANISM;
}

/* turn a mechanism into an oid. */
SECOidTag
PK11_MechanismToAlgtag(CK_MECHANISM_TYPE type)
{
    SECOidData *oid = SECOID_FindOIDByMechanism((unsigned long)type);

    if (oid)
        return oid->offset;
    return SEC_OID_UNKNOWN;
}

/* Determine appropriate blocking mechanism, used when wrapping private keys
 * which require PKCS padding.  If the mechanism does not map to a padding
 * mechanism, we simply return the mechanism.
 */
CK_MECHANISM_TYPE
PK11_GetPadMechanism(CK_MECHANISM_TYPE type)
{
    switch (type) {
        case CKM_SEED_CBC:
            return CKM_SEED_CBC_PAD;
        case CKM_CAMELLIA_CBC:
            return CKM_CAMELLIA_CBC_PAD;
        case CKM_AES_CBC:
            return CKM_AES_CBC_PAD;
        case CKM_DES_CBC:
            return CKM_DES_CBC_PAD;
        case CKM_DES3_CBC:
            return CKM_DES3_CBC_PAD;
        case CKM_RC2_CBC:
            return CKM_RC2_CBC_PAD;
        case CKM_CDMF_CBC:
            return CKM_CDMF_CBC_PAD;
        case CKM_CAST_CBC:
            return CKM_CAST_CBC_PAD;
        case CKM_CAST3_CBC:
            return CKM_CAST3_CBC_PAD;
        case CKM_CAST5_CBC:
            return CKM_CAST5_CBC_PAD;
        case CKM_RC5_CBC:
            return CKM_RC5_CBC_PAD;
        case CKM_IDEA_CBC:
            return CKM_IDEA_CBC_PAD;
        default:
            break;
    }

    return type;
}

static PRBool
pk11_isAllZero(unsigned char *data, int len)
{
    while (len--) {
        if (*data++) {
            return PR_FALSE;
        }
    }
    return PR_TRUE;
}

CK_RV
PK11_MapPBEMechanismToCryptoMechanism(CK_MECHANISM_PTR pPBEMechanism,
                                      CK_MECHANISM_PTR pCryptoMechanism,
                                      SECItem *pbe_pwd, PRBool faulty3DES)
{
    int iv_len = 0;
    CK_PBE_PARAMS_PTR pPBEparams;
    CK_RC2_CBC_PARAMS_PTR rc2_params;
    CK_ULONG rc2_key_len;

    if ((pPBEMechanism == CK_NULL_PTR) || (pCryptoMechanism == CK_NULL_PTR)) {
        return CKR_HOST_MEMORY;
    }

    /* pkcs5 v2 cannot be supported by this interface.
     * use PK11_GetPBECryptoMechanism instead.
     */
    if ((pPBEMechanism->mechanism == CKM_INVALID_MECHANISM) ||
        (pPBEMechanism->mechanism == CKM_PKCS5_PBKD2)) {
        return CKR_MECHANISM_INVALID;
    }

    pPBEparams = (CK_PBE_PARAMS_PTR)pPBEMechanism->pParameter;
    iv_len = PK11_GetIVLength(pPBEMechanism->mechanism);

    if (iv_len) {
        if (pk11_isAllZero(pPBEparams->pInitVector, iv_len)) {
            SECItem param;
            PK11SymKey *symKey;
            PK11SlotInfo *intSlot = PK11_GetInternalSlot();

            if (intSlot == NULL) {
                return CKR_DEVICE_ERROR;
            }

            param.data = pPBEMechanism->pParameter;
            param.len = pPBEMechanism->ulParameterLen;

            symKey = PK11_RawPBEKeyGen(intSlot,
                                       pPBEMechanism->mechanism, &param, pbe_pwd, faulty3DES, NULL);
            PK11_FreeSlot(intSlot);
            if (symKey == NULL) {
                return CKR_DEVICE_ERROR; /* sigh */
            }
            PK11_FreeSymKey(symKey);
        }
    }

    switch (pPBEMechanism->mechanism) {
        case CKM_PBE_MD2_DES_CBC:
        case CKM_PBE_MD5_DES_CBC:
        case CKM_NSS_PBE_SHA1_DES_CBC:
            pCryptoMechanism->mechanism = CKM_DES_CBC;
            goto have_crypto_mechanism;
        case CKM_NSS_PBE_SHA1_TRIPLE_DES_CBC:
        case CKM_NSS_PBE_SHA1_FAULTY_3DES_CBC:
        case CKM_PBE_SHA1_DES3_EDE_CBC:
        case CKM_PBE_SHA1_DES2_EDE_CBC:
            pCryptoMechanism->mechanism = CKM_DES3_CBC;
        have_crypto_mechanism:
            pCryptoMechanism->pParameter = PORT_Alloc(iv_len);
            pCryptoMechanism->ulParameterLen = (CK_ULONG)iv_len;
            if (pCryptoMechanism->pParameter == NULL) {
                return CKR_HOST_MEMORY;
            }
            PORT_Memcpy((unsigned char *)(pCryptoMechanism->pParameter),
                        (unsigned char *)(pPBEparams->pInitVector),
                        iv_len);
            break;
        case CKM_NSS_PBE_SHA1_40_BIT_RC4:
        case CKM_NSS_PBE_SHA1_128_BIT_RC4:
        case CKM_PBE_SHA1_RC4_40:
        case CKM_PBE_SHA1_RC4_128:
            pCryptoMechanism->mechanism = CKM_RC4;
            pCryptoMechanism->ulParameterLen = 0;
            pCryptoMechanism->pParameter = CK_NULL_PTR;
            break;
        case CKM_NSS_PBE_SHA1_40_BIT_RC2_CBC:
        case CKM_PBE_SHA1_RC2_40_CBC:
            rc2_key_len = 40;
            goto have_key_len;
        case CKM_NSS_PBE_SHA1_128_BIT_RC2_CBC:
            rc2_key_len = 128;
        have_key_len:
            pCryptoMechanism->mechanism = CKM_RC2_CBC;
            pCryptoMechanism->ulParameterLen = (CK_ULONG)sizeof(CK_RC2_CBC_PARAMS);
            pCryptoMechanism->pParameter = (CK_RC2_CBC_PARAMS_PTR)
                PORT_ZAlloc(sizeof(CK_RC2_CBC_PARAMS));
            if (pCryptoMechanism->pParameter == NULL) {
                return CKR_HOST_MEMORY;
            }
            rc2_params = (CK_RC2_CBC_PARAMS_PTR)pCryptoMechanism->pParameter;
            PORT_Memcpy((unsigned char *)rc2_params->iv,
                        (unsigned char *)pPBEparams->pInitVector,
                        iv_len);
            rc2_params->ulEffectiveBits = rc2_key_len;
            break;
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/* Make a Key type to an appropriate signing/verification mechanism */
CK_MECHANISM_TYPE
PK11_MapSignKeyType(KeyType keyType)
{
    switch (keyType) {
        case rsaKey:
            return CKM_RSA_PKCS;
        case fortezzaKey:
        case dsaKey:
            return CKM_DSA;
        case ecKey:
            return CKM_ECDSA;
        case edKey:
            return CKM_EDDSA;
        case mldsaKey:
            return CKM_ML_DSA;
        case dhKey:
        case kyberKey:
        default:
            break;
    }
    return CKM_INVALID_MECHANISM;
}

CK_MECHANISM_TYPE
pk11_mapKemKeyType(KeyType keyType)
{
    switch (keyType) {
        case kyberKey:
            return CKM_ML_KEM;
        default:
            break;
    }
    return CKM_INVALID_MECHANISM;
}

CK_MECHANISM_TYPE
pk11_mapWrapKeyType(KeyType keyType)
{
    switch (keyType) {
        case rsaKey:
            return CKM_RSA_PKCS;
        default:
            break;
    }
    return CKM_INVALID_MECHANISM;
}

CK_MECHANISM_TYPE
pk11_mapDeriveKeyType(KeyType keyType)
{
    switch (keyType) {
        case dhKey:
            return CKM_DH_PKCS_DERIVE;
        case ecMontKey:
        case ecKey:
            return CKM_ECDH1_DERIVE;
        /* not adding fortezza here
         * only because forezza is dead */
        default:
            break;
    }
    return CKM_INVALID_MECHANISM;
}

SECOidTag
PK11_FortezzaMapSig(SECOidTag algTag)
{
    switch (algTag) {
        case SEC_OID_MISSI_KEA_DSS:
        case SEC_OID_MISSI_DSS:
        case SEC_OID_MISSI_DSS_OLD:
        case SEC_OID_MISSI_KEA_DSS_OLD:
        case SEC_OID_BOGUS_DSA_SIGNATURE_WITH_SHA1_DIGEST:
            return SEC_OID_ANSIX9_DSA_SIGNATURE;
        default:
            break;
    }
    return algTag;
}
