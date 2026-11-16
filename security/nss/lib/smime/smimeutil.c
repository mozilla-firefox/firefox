/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Stuff specific to S/MIME policy and interoperability.
 */

#include "secmime.h"
#include "secoid.h"
#include "pk11func.h"
#include "ciferfam.h" /* for CIPHER_FAMILY symbols */
#include "secasn1.h"
#include "secitem.h"
#include "sechash.h"
#include "cert.h"
#include "keyhi.h"
#include "secerr.h"
#include "cms.h"
#include "nss.h"
#include "prerror.h"
#include "prinit.h"

SEC_ASN1_MKSUB(CERT_IssuerAndSNTemplate)
SEC_ASN1_MKSUB(SEC_OctetStringTemplate)
SEC_ASN1_CHOOSER_DECLARE(CERT_IssuerAndSNTemplate)

/*
 * XXX Would like the "parameters" field to be a SECItem *, but the
 * encoder is having trouble with optional pointers to an ANY.  Maybe
 * once that is fixed, can change this back...
 */
typedef struct {
    SECItem capabilityID;
    SECItem parameters;
    long cipher; /* optimization */
} NSSSMIMECapability;

static const SEC_ASN1Template NSSSMIMECapabilityTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(NSSSMIMECapability) },
    { SEC_ASN1_OBJECT_ID,
      offsetof(NSSSMIMECapability, capabilityID) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_ANY,
      offsetof(NSSSMIMECapability, parameters) },
    { 0 }
};

static const SEC_ASN1Template NSSSMIMECapabilitiesTemplate[] = {
    { SEC_ASN1_SEQUENCE_OF, 0, NSSSMIMECapabilityTemplate }
};

/*
 * NSSSMIMEEncryptionKeyPreference - if we find one of these, it needs to prompt us
 *  to store this and only this certificate permanently for the sender email address.
 */
typedef enum {
    NSSSMIMEEncryptionKeyPref_IssuerSN,
    NSSSMIMEEncryptionKeyPref_RKeyID,
    NSSSMIMEEncryptionKeyPref_SubjectKeyID
} NSSSMIMEEncryptionKeyPrefSelector;

typedef struct {
    NSSSMIMEEncryptionKeyPrefSelector selector;
    union {
        CERTIssuerAndSN *issuerAndSN;
        NSSCMSRecipientKeyIdentifier *recipientKeyID;
        SECItem *subjectKeyID;
    } id;
} NSSSMIMEEncryptionKeyPreference;

extern const SEC_ASN1Template NSSCMSRecipientKeyIdentifierTemplate[];

static const SEC_ASN1Template smime_encryptionkeypref_template[] = {
    { SEC_ASN1_CHOICE,
      offsetof(NSSSMIMEEncryptionKeyPreference, selector), NULL,
      sizeof(NSSSMIMEEncryptionKeyPreference) },
    { SEC_ASN1_POINTER | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0 | SEC_ASN1_CONSTRUCTED,
      offsetof(NSSSMIMEEncryptionKeyPreference, id.issuerAndSN),
      SEC_ASN1_SUB(CERT_IssuerAndSNTemplate),
      NSSSMIMEEncryptionKeyPref_IssuerSN },
    { SEC_ASN1_POINTER | SEC_ASN1_CONTEXT_SPECIFIC | 1 | SEC_ASN1_CONSTRUCTED,
      offsetof(NSSSMIMEEncryptionKeyPreference, id.recipientKeyID),
      NSSCMSRecipientKeyIdentifierTemplate,
      NSSSMIMEEncryptionKeyPref_RKeyID },
    { SEC_ASN1_POINTER | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 2 | SEC_ASN1_CONSTRUCTED,
      offsetof(NSSSMIMEEncryptionKeyPreference, id.subjectKeyID),
      SEC_ASN1_SUB(SEC_OctetStringTemplate),
      NSSSMIMEEncryptionKeyPref_SubjectKeyID },
    { 0 }
};

/* table of implemented key exchange algorithms. As we add algorithms,
 * update this table */
static const SECOidTag implemented_key_encipherment[] = {
    SEC_OID_PKCS1_RSA_ENCRYPTION,
    SEC_OID_DHSINGLEPASS_STDDH_SHA1KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_STDDH_SHA224KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_STDDH_SHA256KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_STDDH_SHA384KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_STDDH_SHA512KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_COFACTORDH_SHA1KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_COFACTORDH_SHA224KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_COFACTORDH_SHA256KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_COFACTORDH_SHA384KDF_SCHEME,
    SEC_OID_DHSINGLEPASS_COFACTORDH_SHA512KDF_SCHEME,
};
static const int implemented_key_encipherment_len =
    PR_ARRAY_SIZE(implemented_key_encipherment);

/* smime_cipher_map - map of SMIME symmetric "ciphers" to algtag & parameters */
typedef struct {
    unsigned long cipher;
    SECOidTag policytag;
} smime_legacy_map_entry;

/* legacy array of S/MIME values to map old SMIME entries to modern
 * algtags. */
static const smime_legacy_map_entry smime_legacy_map[] = {
    /*    cipher, algtag, policy  */
    /*    ---------------------------------------    */
    { SMIME_RC2_CBC_40, SEC_OID_RC2_40_CBC },
    { SMIME_DES_CBC_56, SEC_OID_DES_CBC },
    { SMIME_RC2_CBC_64, SEC_OID_RC2_64_CBC },
    { SMIME_RC2_CBC_128, SEC_OID_RC2_128_CBC },
    { SMIME_DES_EDE3_168, SEC_OID_DES_EDE3_CBC },
    { SMIME_AES_CBC_128, SEC_OID_AES_128_CBC },
    { SMIME_AES_CBC_256, SEC_OID_AES_256_CBC },
};
static const int smime_legacy_map_count = PR_ARRAY_SIZE(smime_legacy_map);

static int
smime_legacy_pref(SECOidTag algtag)
{
    int i;

    for (i = 0; i < smime_legacy_map_count; i++) {
        if (smime_legacy_map[i].policytag == algtag)
            return i;
    }
    /* GCM is preferred over CBC per RFC 8551, so rank above all legacy
     * algorithms to win same-keysize tiebreaks during sorting. */
    switch (algtag) {
        case SEC_OID_AES_128_GCM:
            return smime_legacy_map_count;
        case SEC_OID_AES_192_GCM:
            return smime_legacy_map_count + 1;
        case SEC_OID_AES_256_GCM:
            return smime_legacy_map_count + 2;
        default:
            break;
    }
    return -1;
}

/* SEC_OID_RC2_CBC is actually 3 ciphers with different key lengths. All modern
 * symmetric ciphers include the key length with the oid. To handle policy for
 * the different keylengths, we include fake oids that let us map the policy based
 * on key length */
static SECOidTag
smime_get_policy_tag_from_key_length(SECOidTag algtag, unsigned long keybits)
{
    if (algtag == SEC_OID_RC2_CBC) {
        switch (keybits) {
            case 40:
                return SEC_OID_RC2_40_CBC;
            case 64:
                return SEC_OID_RC2_64_CBC;
            case 128:
                return SEC_OID_RC2_128_CBC;
            default:
                break;
        }
        return SEC_OID_UNKNOWN;
    }
    return algtag;
}

PRBool
smime_allowed_by_policy(SECOidTag algtag, PRUint32 neededPolicy)
{
    PRUint32 policyFlags;

    /* some S/MIME algs map to the same underlying KEA mechanism,
     * collaps them here */
    if ((neededPolicy & (NSS_USE_ALG_IN_SMIME_KX | NSS_USE_ALG_IN_SMIME_KX_LEGACY)) != 0) {
        CK_MECHANISM_TYPE mechType = PK11_AlgtagToMechanism(algtag);
        switch (mechType) {
            case CKM_ECDH1_DERIVE:
            case CKM_ECDH1_COFACTOR_DERIVE:
                algtag = SEC_OID_ECDH_KEA;
                break;
        }
    }

    if ((NSS_GetAlgorithmPolicy(algtag, &policyFlags) == SECFailure) ||
        ((policyFlags & neededPolicy) != neededPolicy)) {
        PORT_SetError(SEC_ERROR_BAD_EXPORT_ALGORITHM);
        return PR_FALSE;
    }
    return PR_TRUE;
}

/*
 * We'll need this for the fake policy oids for RC2, but the
 * rest of these should be moved to pk11wrap for generic
 * algtag to key size values. We already need this for
 * sec_pkcs5v2_key_length_by oid.
 */
static int
smime_keysize_by_cipher(SECOidTag algtag)
{
    int keysize;

    switch (algtag) {
        case SEC_OID_RC2_40_CBC:
            keysize = 40;
            break;
        case SEC_OID_RC2_64_CBC:
            keysize = 64;
            break;
        case SEC_OID_RC2_128_CBC:
        case SEC_OID_AES_128_CBC:
        case SEC_OID_AES_128_GCM:
        case SEC_OID_CAMELLIA_128_CBC:
            keysize = 128;
            break;
        case SEC_OID_AES_192_CBC:
        case SEC_OID_AES_192_GCM:
        case SEC_OID_CAMELLIA_192_CBC:
            keysize = 192;
            break;
        case SEC_OID_AES_256_CBC:
        case SEC_OID_AES_256_GCM:
        case SEC_OID_CAMELLIA_256_CBC:
            keysize = 256;
            break;
        default:
            keysize = 0;
            break;
    }

    return keysize;
}

static int
smime_max_keysize_by_cipher(SECOidTag algtag)
{
    int keysize = smime_keysize_by_cipher(algtag);

    if (keysize == 0) {
        CK_MECHANISM_TYPE mech = PK11_AlgtagToMechanism(algtag);
        return PK11_GetMaxKeyLength(mech) * PR_BITS_PER_BYTE;
    }
    return keysize;
}

SECOidTag
smime_get_alg_from_policy(SECOidTag policy)
{
    switch (policy) {
        case SEC_OID_RC2_40_CBC:
        case SEC_OID_RC2_64_CBC:
        case SEC_OID_RC2_128_CBC:
            return SEC_OID_RC2_CBC;
        default:
            break;
    }
    return policy;
}

typedef struct SMIMEListStr {
    SECOidTag *tags;
    size_t space_len;
    size_t array_len;
} SMIMEList;

static SMIMEList *smime_algorithm_list = NULL;
static PRLock *algorithm_list_lock = NULL;
static PRCallOnceType smime_init_arg = { 0 };

/* return the number of algorithms in the list */
size_t
smime_list_length(const SMIMEList *list)
{
    if ((list == NULL) || (list->tags == NULL)) {
        return 0;
    }
    return list->array_len;
}

/* find the index of the algtag in the list. If the algtag isn't on the list,
 * return the size of the list */
size_t
smime_list_index_find(const SMIMEList *list, SECOidTag algtag)
{
    int i;
    if ((list == NULL) || (list->tags == NULL)) {
        return 0;
    }
    for (i = 0; i < list->array_len; i++) {
        if (algtag == list->tags[i]) {
            return i;
        }
    }
    return list->array_len;
}

#define SMIME_CHUNK_COUNT 10
/* initialize and grow the list if necessary */
static SECStatus
smime_list_grow(SMIMEList **list)
{
    /* first make sure the inital list is created */
    if (*list == NULL) {
        *list = PORT_ZNew(SMIMEList);
        if (*list == NULL) {
            return SECFailure;
        }
    }
    /* make sure the tag array is intialized */
    if ((*list)->tags == NULL) {
        (*list)->tags = PORT_ZNewArray(SECOidTag, SMIME_CHUNK_COUNT);
        if ((*list)->tags == NULL) {
            return SECFailure;
        }
        (*list)->space_len = SMIME_CHUNK_COUNT;
    }
    /* grow the tag array if necessary */
    if ((*list)->array_len == (*list)->space_len) {
        SECOidTag *new_space;
        size_t new_len = (*list)->space_len + SMIME_CHUNK_COUNT;
        new_space = (SECOidTag *)PORT_Realloc((*list)->tags,
                                              new_len * sizeof(SECOidTag));
        if (new_space == NULL) {
            return SECFailure;
        }
        (*list)->tags = new_space;
        (*list)->space_len = new_len;
    }
    return SECSuccess;
}

/* add a new algtag to the list. if the algtag is already on the list,
 * do nothing */
static SECStatus
smime_list_add(SMIMEList **list, SECOidTag algtag)
{
    SECStatus rv;
    size_t array_len = smime_list_length(*list);
    size_t c_index = smime_list_index_find(*list, algtag);

    if (array_len != c_index) {
        /* already on the list */
        return SECSuccess;
    }

    /* go the list if necessary */
    rv = smime_list_grow(list);
    if (rv != SECSuccess) {
        return rv;
    }
    (*list)->tags[(*list)->array_len++] = algtag;
    return SECSuccess;
}

static SECOidTag
smime_list_fetch_by_index(const SMIMEList *list, size_t c_index)
{
    size_t cipher_count = smime_list_length(list);

    if (c_index >= cipher_count) {
        return SEC_OID_UNKNOWN;
    }
    /* we know this is safe because list cipher_count is non-zero (if it were
     * any value of c_index will cause the above if to trigger */
    return list->tags[c_index];
}

static void
smime_free_list(SMIMEList **list)
{
    if (*list) {
        if ((*list)->tags) {
            PORT_Free((*list)->tags);
        }
        PORT_Free(*list);
    }
    *list = NULL;
}

static void
smime_lock_algorithm_list(void)
{
    PORT_Assert(algorithm_list_lock);
    if (algorithm_list_lock) {
        PR_Lock(algorithm_list_lock);
    }
    return;
}

static void
smime_unlock_algorithm_list(void)
{
    PORT_Assert(algorithm_list_lock);
    if (algorithm_list_lock) {
        PR_Unlock(algorithm_list_lock);
    }
    return;
}

static SECStatus
smime_shutdown(void *appData, void *nssData)
{
    if (algorithm_list_lock) {
        PR_DestroyLock(algorithm_list_lock);
        algorithm_list_lock = NULL;
    }
    smime_free_list(&smime_algorithm_list);
    memset(&smime_init_arg, 0, sizeof(smime_init_arg));
    return SECSuccess;
}

static PRStatus
smime_init_once(void *arg)
{
    SECOidTag *tags = NULL;
    SECStatus rv;
    int tagCount;
    int i;
    int *error = (int *)arg;
    int *lengths = NULL;
    int *legacy_prefs = NULL;

    rv = NSS_RegisterShutdown(smime_shutdown, NULL);
    if (rv != SECSuccess) {
        *error = PORT_GetError();
        return PR_FAILURE;
    }
    algorithm_list_lock = PR_NewLock();
    if (algorithm_list_lock == NULL) {
        *error = PORT_GetError();
        return PR_FAILURE;
    }

    /* At initialization time, we need to set up the defaults. We first
     * look to see if the system or application has set up certain algorithms
     * by policy. If they have set up values by policy we'll only allow those
     * algorithms. We'll then look to see if any algorithms are enabled by
     * the application. */
    rv = NSS_GetAlgorithmPolicyAll(NSS_USE_ALG_IN_SMIME_LEGACY,
                                   NSS_USE_ALG_IN_SMIME_LEGACY,
                                   &tags, &tagCount);
    if (tags) {
        PORT_Free(tags);
        tags = NULL;
    }
    if ((rv != SECSuccess) || (tagCount == 0)) {
        /* No algorithms have been enabled by policy (either by the system
         * or by the application, we then will use the traditional default
         * algorithms from the policy map */
        /* Enable NSS_USE_ALG_IN_SMIME for GCM so the capability loop
         * advertises it; do not add to smime_algorithm_list (encoding
         * is not yet supported). */
        NSS_SetAlgorithmPolicy(SEC_OID_AES_128_GCM, NSS_USE_ALG_IN_SMIME, 0);
        NSS_SetAlgorithmPolicy(SEC_OID_AES_192_GCM, NSS_USE_ALG_IN_SMIME, 0);
        NSS_SetAlgorithmPolicy(SEC_OID_AES_256_GCM, NSS_USE_ALG_IN_SMIME, 0);
        for (i = smime_legacy_map_count - 1; i >= 0; i--) {
            SECOidTag policytag = smime_legacy_map[i].policytag;
            /* this enables the algorithm by policy. We need this or
             * the policy code will reject attempts to use it */
            NSS_SetAlgorithmPolicy(policytag, NSS_USE_ALG_IN_SMIME, 0);
            /* We also need to enable the algorithm. This is usually unde
             * application control once the defaults are set up, so the
             * application can turn off a policy that is already on, but
             * not turn on a policy that is already off */
            smime_list_add(&smime_algorithm_list, policytag);
        }
        return PR_SUCCESS;
    }
    /* We have a system supplied policy, do we also have
     * system supplied defaults? If we do we will only actually
     * turn on the algorithms that have been specified. */
    rv = NSS_GetAlgorithmPolicyAll(NSS_USE_DEFAULT_NOT_VALID |
                                       NSS_USE_DEFAULT_SMIME_ENABLE,
                                   NSS_USE_DEFAULT_SMIME_ENABLE,
                                   &tags, &tagCount);
    /* if none found, enable the default algorithms */
    if ((rv != SECSuccess) || (tagCount == 0)) {
        if (tags) {
            PORT_Free(tags);
            tags = NULL;
        }
        /* GCM omitted from smime_algorithm_list (see above);
         * advertised in NSS_SMIMEUtil_CreateSMIMECapabilities. */
        for (i = smime_legacy_map_count - 1; i >= 0; i--) {
            SECOidTag policytag = smime_legacy_map[i].policytag;
            /* we only enable the default algorithm, we don't change
             * it's policy, which the system has already set. NOTE:
             * what 'enable' means in the S/MIME sense is we advertise
             * that we can do the given algorithm in our smime capabilities. */
            smime_list_add(&smime_algorithm_list, policytag);
        }
        return PR_SUCCESS;
    }

    /* Sort tags by key strength here */
    lengths = PORT_ZNewArray(int, tagCount);
    if (lengths == NULL) {
        *error = PORT_GetError();
        goto loser;
    }
    legacy_prefs = PORT_ZNewArray(int, tagCount);
    if (legacy_prefs == NULL) {
        *error = PORT_GetError();
        goto loser;
    }
    /* Sort the tags array, highest preference at index 0 */
    for (i = 0; i < tagCount; i++) {
        int len = smime_max_keysize_by_cipher(tags[i]);
        int lpref = smime_legacy_pref(tags[i]);
        SECOidTag current = tags[i];
        PRBool shift = PR_FALSE;
        int j;
        /* Determine best position for tags[i].
         * For each position j, check if tags [i] has a higher preference.
         * If yes, store tags[i] at position j, and move all following
         * entries one position to the back of the array.
         */
        for (j = 0; j < i; j++) {
            int tlen = lengths[j];
            int tpref = legacy_prefs[j];
            SECOidTag ttag = tags[j];
            /* we prefer ciphers with bigger keysizes, then
             * we prefer ciphers in our historical list,
             * then we prefer ciphers that show up first
             * from the oid table */
            if (shift || (len > tlen) || ((len == tlen) && (lpref > tpref))) {
                tags[j] = current;
                lengths[j] = len;
                legacy_prefs[j] = lpref;
                current = ttag;
                len = tlen;
                lpref = tpref;
                shift = PR_TRUE;
            }
        }
        tags[i] = current;
        lengths[i] = len;
        legacy_prefs[i] = lpref;
    }

    /* put them in the enable list */
    for (i = 0; i < tagCount; i++) {
        /* AEAD ciphers must not enter the outgoing cipher vote table
         * until AuthEnvelopedData encoding is supported. */
        if (PK11_IsAEAD(PK11_AlgtagToMechanism(tags[i]))) {
            continue;
        }
        smime_list_add(&smime_algorithm_list, tags[i]);
    }
    PORT_Free(lengths);
    PORT_Free(legacy_prefs);
    PORT_Free(tags);
    return PR_SUCCESS;
loser:
    if (lengths)
        PORT_Free(lengths);
    if (legacy_prefs)
        PORT_Free(legacy_prefs);
    if (tags)
        PORT_Free(tags);
    return PR_FAILURE;
}

static SECStatus
smime_init(void)
{
    static PRBool smime_policy_initted = PR_FALSE;
    static int error = 0;
    PRStatus nrv;

    /* has NSS been initialized? */
    if (!NSS_IsInitialized()) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return SECFailure;
    }
    if (smime_policy_initted) {
        return SECSuccess;
    }
    nrv = PR_CallOnceWithArg(&smime_init_arg, smime_init_once, &error);
    if (nrv == PR_SUCCESS) {
        smime_policy_initted = PR_TRUE;
        return SECSuccess;
    }
    PORT_SetError(error);
    return SECFailure;
}

PRBool
NSS_SMIMEUtil_DecryptionAllowed(SECAlgorithmID *algid, PK11SymKey *key)
{
    SECOidTag algtag;
    /* make sure we are inited before checking policy, so
     * the defaults are correct */
    SECStatus rv = smime_init();
    if (rv != SECSuccess) {
        return SECFailure;
    }

    algtag = smime_get_policy_tag_from_key_length(SECOID_GetAlgorithmTag(algid),
                                                  PK11_GetKeyStrength(key, algid));
    return smime_allowed_by_policy(algtag, NSS_USE_ALG_IN_SMIME_LEGACY);
}

PRBool
NSS_SMIMEUtil_EncryptionAllowed(SECAlgorithmID *algid, PK11SymKey *key)
{
    SECOidTag algtag;
    /* make sure we are inited before checking policy, so
     * the defaults are correct */
    SECStatus rv = smime_init();
    if (rv != SECSuccess) {
        return SECFailure;
    }

    algtag = smime_get_policy_tag_from_key_length(SECOID_GetAlgorithmTag(algid),
                                                  PK11_GetKeyStrength(key, algid));
    return smime_allowed_by_policy(algtag, NSS_USE_ALG_IN_SMIME);
}

PRBool
NSS_SMIMEUtil_SigningAllowed(SECAlgorithmID *algid)
{
    SECOidTag algtag;
    /* we don't adjust SIGNATURE policy based on defaults, so no need
     * to call smime_init() */

    algtag = SECOID_GetAlgorithmTag(algid);
    return smime_allowed_by_policy(algtag, NSS_USE_ALG_IN_SMIME_SIGNATURE);
}

static PRBool
nss_smime_enforce_key_size(void)
{
    PRInt32 optFlags;

    if (NSS_OptionGet(NSS_KEY_SIZE_POLICY_FLAGS, &optFlags) != SECFailure) {
        if (optFlags & NSS_KEY_SIZE_POLICY_SMIME_FLAG) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

PRBool
NSS_SMIMEUtil_KeyEncodingAllowed(SECAlgorithmID *algid, CERTCertificate *cert,
                                 SECKEYPublicKey *key)
{
    SECOidTag algtag;
    /* we don't adjust KEA policy based on defaults, so no need
     * to call smime_init() */

    /* if required, make sure the key lengths are enforced */
    if (nss_smime_enforce_key_size()) {
        SECStatus rv;
        PRBool freeKey = PR_FALSE;

        if (!key) {
            /* either the public key or the cert must be supplied. If the
             * key wasn't supplied, get it from the certificate */
            if (!cert) {
                PORT_SetError(SEC_ERROR_INVALID_ARGS);
                return PR_FALSE;
            }
            key = CERT_ExtractPublicKey(cert);
            freeKey = PR_TRUE;
        }
        rv = SECKEY_EnforceKeySize(key->keyType,
                                   SECKEY_PublicKeyStrengthInBits(key),
                                   SEC_ERROR_BAD_EXPORT_ALGORITHM);
        if (freeKey) {
            SECKEY_DestroyPublicKey(key);
        }
        if (rv != SECSuccess) {
            return PR_FALSE;
        }
    }
    algtag = SECOID_GetAlgorithmTag(algid);
    return smime_allowed_by_policy(algtag, NSS_USE_ALG_IN_SMIME_KX);
}

PRBool
NSS_SMIMEUtil_KeyDecodingAllowed(SECAlgorithmID *algid, SECKEYPrivateKey *key)
{
    SECOidTag algtag;
    /* we don't adjust KEA policy based on defaults, so no need
     * to call smime_init() */

    /* if required, make sure the key lengths are enforced */
    if (nss_smime_enforce_key_size()) {
        SECStatus rv;
        rv = SECKEY_EnforceKeySize(key->keyType,
                                   SECKEY_PrivateKeyStrengthInBits(key),
                                   SEC_ERROR_BAD_EXPORT_ALGORITHM);
        if (rv != SECSuccess) {
            return PR_FALSE;
        }
    }
    algtag = SECOID_GetAlgorithmTag(algid);
    return smime_allowed_by_policy(algtag, NSS_USE_ALG_IN_SMIME_KX_LEGACY);
}

static SECOidTag
nss_SMIME_FindCipherForSMIMECap(PLArenaPool *poolp, NSSSMIMECapability *cap)
{
    SECOidTag capIDTag;

    /* we need the OIDTag here */
    capIDTag = SECOID_FindOIDTag(&(cap->capabilityID));

    /* RC2 used a generic oid and encoded the key length in the
     * parameters */
    if (capIDTag == SEC_OID_RC2_CBC) {
        SECStatus rv;
        unsigned long key_bits;
        SECItem keyItem = { siBuffer, NULL, 0 };

        /* The parameters were captured as an ANY by the QuickDER decode of
         * the capabilities, so they are a single TLV with a DER length.
         * For such input QuickDER and the streaming decoder accept the same
         * INTEGERs and return the same contents. QuickDER is used because
         * the profile comes from a remote sender. keyItem points into
         * cap->parameters and needs no freeing. */
        rv = SEC_QuickDERDecodeItem(poolp, &keyItem,
                                    SEC_ASN1_GET(SEC_IntegerTemplate),
                                    &cap->parameters);
        if (rv != SECSuccess) {
            return SEC_OID_UNKNOWN;
        }
        rv = SEC_ASN1DecodeInteger(&keyItem, &key_bits);
        if (rv != SECSuccess) {
            return SEC_OID_UNKNOWN;
        }
        return smime_get_policy_tag_from_key_length(capIDTag, key_bits);
    }

    /* everything else uses a null parameter */
    if (!cap->parameters.data || !cap->parameters.len) {
        return capIDTag;
    }
    if (cap->parameters.len == 2 &&
        cap->parameters.data[0] == SEC_ASN1_NULL &&
        cap->parameters.data[1] == 0) {
        return capIDTag;
    }
    return SEC_OID_UNKNOWN;
}

/*
 * smime_choose_cipher - choose a cipher that works for all the recipients
 *
 * "rcerts" - recipient's certificates
 */
static SECOidTag
smime_choose_cipher(CERTCertificate **rcerts)
{
    PLArenaPool *poolp = NULL;
    SECOidTag chosen_cipher = SEC_OID_UNKNOWN;
    size_t cipher_count;
    SECOidTag cipher;
    int *cipher_abilities;
    int *cipher_votes;
    size_t weak_index;
    size_t strong_index;
    size_t aes128_index;
    size_t aes256_index;
    size_t c_index;
    int rcount, max;

    smime_lock_algorithm_list();
    cipher_count = smime_list_length(smime_algorithm_list);
    if (cipher_count == 0) {
        goto done;
    }

    chosen_cipher = SEC_OID_RC2_40_CBC; /* the default, LCD */
    weak_index = smime_list_index_find(smime_algorithm_list, chosen_cipher);
    strong_index = smime_list_index_find(smime_algorithm_list, SEC_OID_DES_EDE3_CBC);
    aes128_index = smime_list_index_find(smime_algorithm_list, SEC_OID_AES_128_CBC);
    aes256_index = smime_list_index_find(smime_algorithm_list, SEC_OID_AES_256_CBC);
    /* make sure the default selected cipher is enabled */
    if (weak_index == cipher_count) {
        chosen_cipher = SEC_OID_DES_EDE3_CBC;
        if (strong_index == cipher_count) {
            chosen_cipher = SEC_OID_AES_128_CBC;
            if (aes128_index == cipher_count) {
                chosen_cipher = SEC_OID_AES_256_CBC;
                if (aes256_index == cipher_count) {
                    /* none of the standard algorithms are enabled, If the
                     * recipients don't explicitly include a better cipher
                     * then fail */
                    chosen_cipher = SEC_OID_UNKNOWN;
                }
            }
        }
    }

    poolp = PORT_NewArena(1024); /* XXX what is right value? */
    if (poolp == NULL)
        goto done;

    cipher_abilities = PORT_ArenaZNewArray(poolp, int, cipher_count + 1);
    cipher_votes = PORT_ArenaZNewArray(poolp, int, cipher_count + 1);
    if (cipher_votes == NULL || cipher_abilities == NULL) {
        goto done;
    }

    /* Make triple-DES the strong cipher. */

    /* walk all the recipient's certs */
    for (rcount = 0; rcerts[rcount] != NULL; rcount++) {
        SECItem *profile;
        NSSSMIMECapability **caps;
        int pref;

        /* the first cipher that matches in the user's SMIME profile gets
         * "cipher_count" votes; the next one gets "cipher_count" - 1
         * and so on. If every cipher matches, the last one gets 1 (one) vote */
        pref = cipher_count;

        /* find recipient's SMIME profile */
        profile = CERT_FindSMimeProfile(rcerts[rcount]);

        if (profile != NULL && profile->data != NULL && profile->len > 0) {
            /* we have a profile (still DER-encoded) */
            caps = NULL;
            /* decode it */
            if (SEC_QuickDERDecodeItem(poolp, &caps,
                                       NSSSMIMECapabilitiesTemplate, profile) == SECSuccess &&
                caps != NULL) {
                int i;
                /* walk the SMIME capabilities for this recipient */
                for (i = 0; caps[i] != NULL; i++) {
                    cipher = nss_SMIME_FindCipherForSMIMECap(poolp, caps[i]);
                    c_index = smime_list_index_find(smime_algorithm_list, cipher);
                    if (c_index < cipher_count) {
                        /* found the cipher */
                        cipher_abilities[c_index]++;
                        cipher_votes[c_index] += pref;
                        --pref;
                    }
                }
            }
        } else {
            /* no profile found - so we can only assume that the user can do
             * the mandatory algorithms which are RC2-40 (weak crypto) and
             * 3DES (strong crypto), unless the user has an elliptic curve
             * key.  For elliptic curve keys, RFC 5753 mandates support
             * for AES 128 CBC. */
            SECKEYPublicKey *key;
            unsigned int pklen_bits;
            KeyType key_type;

            /*
             * if recipient's public key length is > 512, vote for a strong cipher
             * please not that the side effect of this is that if only one recipient
             * has an export-level public key, the strong cipher is disabled.
             *
             * XXX This is probably only good for RSA keys.  What I would
             * really like is a function to just say;  Is the public key in
             * this cert an export-length key?  Then I would not have to
             * know things like the value 512, or the kind of key, or what
             * a subjectPublicKeyInfo is, etc.
             */
            key = CERT_ExtractPublicKey(rcerts[rcount]);
            pklen_bits = 0;
            key_type = nullKey;
            if (key != NULL) {
                pklen_bits = SECKEY_PublicKeyStrengthInBits(key);
                key_type = SECKEY_GetPublicKeyType(key);
                SECKEY_DestroyPublicKey(key);
                key = NULL;
            }

            if (key_type == ecKey) {
                /* While RFC 5753 mandates support for AES-128 CBC, should use
                 * AES 256 if user's key provides more than 128 bits of
                 * security strength so that symmetric key is not weak link. */

                /* RC2-40 is not compatible with elliptic curve keys. */
                if (chosen_cipher == SEC_OID_RC2_40_CBC) {
                    chosen_cipher = SEC_OID_AES_128_CBC;
                }
                if (pklen_bits > 256) {
                    cipher_abilities[aes256_index]++;
                    cipher_votes[aes256_index] += pref;
                    pref--;
                }
                cipher_abilities[aes128_index]++;
                cipher_votes[aes128_index] += pref;
                pref--;
                cipher_abilities[strong_index]++;
                cipher_votes[strong_index] += pref;
                pref--;
            } else {
                if (pklen_bits > 3072) {
                    /* While support for AES 256 is a SHOULD+ in RFC 5751
                     * rather than a MUST, RSA and DSA keys longer than 3072
                     * bits provide more than 128 bits of security strength.
                     * So, AES 256 should be used to provide comparable
                     * security. */
                    cipher_abilities[aes256_index]++;
                    cipher_votes[aes256_index] += pref;
                    pref--;
                }
                if (pklen_bits > 1023) {
                    /* RFC 5751 mandates support for AES 128, but also says
                     * that RSA and DSA signature keys SHOULD NOT be less than
                     * 1024 bits. So, cast vote for AES 128 if key length
                     * is at least 1024 bits. */
                    cipher_abilities[aes128_index]++;
                    cipher_votes[aes128_index] += pref;
                    pref--;
                }
                if (pklen_bits > 512) {
                    /* cast votes for the strong algorithm */
                    cipher_abilities[strong_index]++;
                    cipher_votes[strong_index] += pref;
                    pref--;
                }

                /* always cast (possibly less) votes for the weak algorithm */
                cipher_abilities[weak_index]++;
                cipher_votes[weak_index] += pref;
            }
        }
        if (profile != NULL)
            SECITEM_FreeItem(profile, PR_TRUE);
    }

    /* find cipher that is agreeable by all recipients and that has the most votes */
    max = 0;
    for (c_index = 0; c_index < cipher_count; c_index++) {
        /* if not all of the recipients can do this, forget it */
        if (cipher_abilities[c_index] != rcount)
            continue;
        cipher = smime_list_fetch_by_index(smime_algorithm_list, c_index);
        /* if cipher is allowed by policy, forget it */
        if (!smime_allowed_by_policy(cipher, NSS_USE_ALG_IN_SMIME)) {
            continue;
        }
        /* now see if this one has more votes than the last best one */
        if (cipher_votes[c_index] >= max) {
            /* if equal number of votes, prefer the ones further down in the list */
            /* with the expectation that these are higher rated ciphers */
            chosen_cipher = cipher;
            max = cipher_votes[c_index];
        }
    }
    /* if no common cipher was found, chosen_cipher stays at the default */

done:
    smime_unlock_algorithm_list();
    if (poolp != NULL)
        PORT_FreeArena(poolp, PR_FALSE);

    return chosen_cipher;
}

/*
 * NSS_SMIMEUtil_FindBulkAlgForRecipients - find bulk algorithm suitable for all recipients
 *
 * it would be great for UI purposes if there would be a way to find out which recipients
 * prevented a strong cipher from being used...
 */
SECStatus
NSS_SMIMEUtil_FindBulkAlgForRecipients(CERTCertificate **rcerts,
                                       SECOidTag *bulkalgtag, int *keysize)
{
    SECOidTag cipher;

    SECStatus rv = smime_init();
    if (rv != SECSuccess) {
        return SECFailure;
    }

    cipher = smime_choose_cipher(rcerts);
    if (cipher == SEC_OID_UNKNOWN) {
        PORT_SetError(SEC_ERROR_BAD_EXPORT_ALGORITHM);
        return SECFailure;
    }

    *bulkalgtag = smime_get_alg_from_policy(cipher);
    *keysize = smime_keysize_by_cipher(cipher);

    return SECSuccess;
}

/*
 * Create a new Capability from an oid tag
 */
static NSSSMIMECapability *
smime_create_capability(SECOidTag cipher)
{
    NSSSMIMECapability *cap = NULL;
    SECOidData *oiddata = NULL;
    SECItem *dummy = NULL;

    oiddata = SECOID_FindOIDByTag(smime_get_alg_from_policy(cipher));
    if (oiddata == NULL) {
        return NULL;
    }

    cap = PORT_ZNew(NSSSMIMECapability);
    if (cap == NULL) {
        return NULL;
    }

    cap->capabilityID.data = oiddata->oid.data;
    cap->capabilityID.len = oiddata->oid.len;
    if (cipher == SEC_OID_RC2_CBC) {
        SECItem keyItem = { siBuffer, NULL, 0 };
        unsigned long keybits = smime_get_alg_from_policy(cipher);
        dummy = SEC_ASN1EncodeInteger(NULL, &keyItem, keybits);
        if (dummy == NULL) {
            PORT_Free(cap);
            return NULL;
        }
        dummy = SEC_ASN1EncodeItem(NULL, &cap->parameters,
                                   &keyItem, SEC_ASN1_GET(SEC_IntegerTemplate));
        SECITEM_FreeItem(&keyItem, PR_FALSE);
        if (dummy == NULL) {
            PORT_Free(cap);
            return NULL;
        }
    } else {
        cap->parameters.data = NULL;
        cap->parameters.len = 0;
    }
    return cap;
}
/*
 * NSS_SMIMEUtil_CreateSMIMECapabilities - get S/MIME capabilities for this instance of NSS
 *
 * scans the list of allowed and enabled ciphers and construct a PKCS9-compliant
 * S/MIME capabilities attribute value.
 *
 * XXX Please note that, in contradiction to RFC2633 2.5.2, the capabilities only include
 * symmetric ciphers, NO signature algorithms or key encipherment algorithms.
 *
 * "poolp" - arena pool to create the S/MIME capabilities data on
 * "dest" - SECItem to put the data in
 */
SECStatus
NSS_SMIMEUtil_CreateSMIMECapabilities(PLArenaPool *poolp, SECItem *dest)
{
    NSSSMIMECapability *cap = NULL;
    NSSSMIMECapability **smime_capabilities = NULL;
    SECItem *dummy = NULL;
    int i, capIndex;
    int cap_count;
    int cipher_count;
    int hash_count;
    static const SECOidTag gcm_caps[] = {
        SEC_OID_AES_256_GCM, SEC_OID_AES_192_GCM, SEC_OID_AES_128_GCM
    };

    SECStatus rv = smime_init();
    if (rv != SECSuccess) {
        return SECFailure;
    }
    /* First get the hash count */
    for (i = HASH_AlgNULL + 1;; i++) {
        if (HASH_GetHashOidTagByHashType(i) == SEC_OID_UNKNOWN) {
            break;
        }
    }
    hash_count = i - 1;

    smime_lock_algorithm_list();
    /* now get the cipher count */
    cipher_count = smime_list_length(smime_algorithm_list);
    if (cipher_count == 0) {
        smime_unlock_algorithm_list();
        PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
        return SECFailure;
    }

    cap_count = cipher_count + hash_count + implemented_key_encipherment_len +
                (int)PR_ARRAY_SIZE(gcm_caps);

    /* cipher_count + 1 is an upper bound - we might end up with less */
    smime_capabilities = PORT_ZNewArray(NSSSMIMECapability *, cap_count + 1);
    if (smime_capabilities == NULL) {
        smime_unlock_algorithm_list();
        return SECFailure;
    }

    capIndex = 0;

    /* RFC 8551: GCM preferred, advertised first. AuthEnvelopedData
     * decryption is supported, so advertise GCM unconditionally; the
     * encoder side stays gated by smime_algorithm_list (which omits
     * GCM) until AuthEnvelopedData encoding is supported. */
    for (i = 0; i < (int)PR_ARRAY_SIZE(gcm_caps); i++) {
        if (!smime_allowed_by_policy(gcm_caps[i], NSS_USE_ALG_IN_SMIME)) {
            continue;
        }
        cap = smime_create_capability(gcm_caps[i]);
        if (cap == NULL)
            break;
        smime_capabilities[capIndex++] = cap;
    }

    /* Add all the symmetric ciphers
     * We walk the cipher list,  as it is ordered by decreasing strength,
     * we prefer the stronger cipher over a weaker one, and we have to list the
     * preferred algorithm first */
    for (i = 0; i < cipher_count; i++) {
        SECOidTag cipher = smime_list_fetch_by_index(smime_algorithm_list, i);

        /* is it allowed by policy? */
        if (!smime_allowed_by_policy(cipher, NSS_USE_ALG_IN_SMIME)) {
            continue;
        }
        cipher = smime_get_alg_from_policy(cipher);

        cap = smime_create_capability(cipher);
        if (cap == NULL)
            break;
        smime_capabilities[capIndex++] = cap;
    }
    /* add signature algorithms = hash algs.
     * probably also need to figure how what
     * actual signatures we support in secvfy
     * as well. We currently don't look a these
     * when choosing hash and signature (hash is
     * chosen by the application and signature
     * type is chosen by the signing cert/key) */
    smime_unlock_algorithm_list();
    for (i = HASH_AlgNULL + 1; i < hash_count + 1; i++) {
        SECOidTag hash_alg = HASH_GetHashOidTagByHashType(i);

        if (!smime_allowed_by_policy(hash_alg,
                                     NSS_USE_ALG_IN_SMIME_SIGNATURE | NSS_USE_ALG_IN_SIGNATURE)) {
            continue;
        }
        cap = smime_create_capability(hash_alg);
        /* get next SMIME capability */
        if (cap == NULL)
            break;
        smime_capabilities[capIndex++] = cap;
    }

    /* add key encipherment algorithms . These are static
     * to the s/mime library, so we can just use the table.
     * new kea algs should be implemented. We don't use these
     * because the senders key pretty much selects what time
     * of kea we are going to implement */
    for (i = 0; i < implemented_key_encipherment_len; i++) {
        SECOidTag kea_alg = implemented_key_encipherment[i];

        if (!smime_allowed_by_policy(kea_alg, NSS_USE_ALG_IN_SMIME_KX)) {
            continue;
        }
        cap = smime_create_capability(kea_alg);
        /* get next SMIME capability */
        if (cap == NULL)
            break;
        smime_capabilities[capIndex++] = cap;
    }

    smime_capabilities[capIndex] = NULL; /* last one - now encode */
    dummy = SEC_ASN1EncodeItem(poolp, dest, &smime_capabilities, NSSSMIMECapabilitiesTemplate);

    /* now that we have the proper encoded SMIMECapabilities (or not),
     * free the work data */
    for (i = 0; smime_capabilities[i] != NULL; i++) {
        if (smime_capabilities[i]->parameters.data) {
            PORT_Free(smime_capabilities[i]->parameters.data);
        }
        PORT_Free(smime_capabilities[i]);
    }
    PORT_Free(smime_capabilities);

    return (dummy == NULL) ? SECFailure : SECSuccess;
}

/*
 * NSS_SMIMEUtil_CreateSMIMEEncKeyPrefs - create S/MIME encryption key preferences attr value
 *
 * "poolp" - arena pool to create the attr value on
 * "dest" - SECItem to put the data in
 * "cert" - certificate that should be marked as preferred encryption key
 *          cert is expected to have been verified for EmailRecipient usage.
 */
SECStatus
NSS_SMIMEUtil_CreateSMIMEEncKeyPrefs(PLArenaPool *poolp, SECItem *dest, CERTCertificate *cert)
{
    NSSSMIMEEncryptionKeyPreference ekp;
    SECItem *dummy = NULL;
    PLArenaPool *tmppoolp = NULL;

    if (cert == NULL)
        goto loser;

    tmppoolp = PORT_NewArena(1024);
    if (tmppoolp == NULL)
        goto loser;

    /* XXX hardcoded IssuerSN choice for now */
    ekp.selector = NSSSMIMEEncryptionKeyPref_IssuerSN;
    ekp.id.issuerAndSN = CERT_GetCertIssuerAndSN(tmppoolp, cert);
    if (ekp.id.issuerAndSN == NULL)
        goto loser;

    dummy = SEC_ASN1EncodeItem(poolp, dest, &ekp, smime_encryptionkeypref_template);

loser:
    if (tmppoolp)
        PORT_FreeArena(tmppoolp, PR_FALSE);

    return (dummy == NULL) ? SECFailure : SECSuccess;
}

/*
 * NSS_SMIMEUtil_CreateSMIMEEncKeyPrefs - create S/MIME encryption key preferences attr value using MS oid
 *
 * "poolp" - arena pool to create the attr value on
 * "dest" - SECItem to put the data in
 * "cert" - certificate that should be marked as preferred encryption key
 *          cert is expected to have been verified for EmailRecipient usage.
 */
SECStatus
NSS_SMIMEUtil_CreateMSSMIMEEncKeyPrefs(PLArenaPool *poolp, SECItem *dest, CERTCertificate *cert)
{
    SECItem *dummy = NULL;
    PLArenaPool *tmppoolp = NULL;
    CERTIssuerAndSN *isn;

    if (cert == NULL)
        goto loser;

    tmppoolp = PORT_NewArena(1024);
    if (tmppoolp == NULL)
        goto loser;

    isn = CERT_GetCertIssuerAndSN(tmppoolp, cert);
    if (isn == NULL)
        goto loser;

    dummy = SEC_ASN1EncodeItem(poolp, dest, isn, SEC_ASN1_GET(CERT_IssuerAndSNTemplate));

loser:
    if (tmppoolp)
        PORT_FreeArena(tmppoolp, PR_FALSE);

    return (dummy == NULL) ? SECFailure : SECSuccess;
}

/*
 * NSS_SMIMEUtil_GetCertFromEncryptionKeyPreference -
 *                              find cert marked by EncryptionKeyPreference attribute
 *
 * "certdb" - handle for the cert database to look in
 * "DERekp" - DER-encoded value of S/MIME Encryption Key Preference attribute
 *
 * if certificate is supposed to be found among the message's included certificates,
 * they are assumed to have been imported already.
 */
CERTCertificate *
NSS_SMIMEUtil_GetCertFromEncryptionKeyPreference(CERTCertDBHandle *certdb, SECItem *DERekp)
{
    PLArenaPool *tmppoolp = NULL;
    CERTCertificate *cert = NULL;
    NSSSMIMEEncryptionKeyPreference ekp;

    tmppoolp = PORT_NewArena(1024);
    if (tmppoolp == NULL)
        return NULL;

    /* decode DERekp */
    if (SEC_QuickDERDecodeItem(tmppoolp, &ekp, smime_encryptionkeypref_template,
                               DERekp) != SECSuccess)
        goto loser;

    /* find cert */
    switch (ekp.selector) {
        case NSSSMIMEEncryptionKeyPref_IssuerSN:
            cert = CERT_FindCertByIssuerAndSN(certdb, ekp.id.issuerAndSN);
            break;
        case NSSSMIMEEncryptionKeyPref_RKeyID:
        case NSSSMIMEEncryptionKeyPref_SubjectKeyID:
            /* XXX not supported yet - we need to be able to look up certs by SubjectKeyID */
            break;
        default:
            PORT_Assert(0);
    }
loser:
    if (tmppoolp)
        PORT_FreeArena(tmppoolp, PR_FALSE);

    return cert;
}

extern const char __nss_smime_version[];

PRBool
NSSSMIME_VersionCheck(const char *importedVersion)
{
#define NSS_VERSION_VARIABLE __nss_smime_version
#include "verref.h"
    /*
     * This is the secret handshake algorithm.
     *
     * This release has a simple version compatibility
     * check algorithm.  This release is not backward
     * compatible with previous major releases.  It is
     * not compatible with future major, minor, or
     * patch releases.
     */
    return NSS_VersionCheck(importedVersion);
}

const char *
NSSSMIME_GetVersion(void)
{
    return NSS_VERSION;
}
