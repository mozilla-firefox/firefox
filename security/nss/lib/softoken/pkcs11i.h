/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Internal data structures and functions used by pkcs11.c
 */
#ifndef _PKCS11I_H_
#define _PKCS11I_H_ 1

#include "prlock.h"
#include "seccomon.h"
#include "secoidt.h"
#include "lowkeyti.h"
#include "pkcs11t.h"

#include "sftkdbt.h"
#include "chacha20poly1305.h"
#include "hasht.h"

#include "alghmac.h"
#include "cmac.h"

/*
 * Configuration Defines
 *
 * The following defines affect the space verse speed trade offs of
 * the PKCS #11 module. For the most part the current settings are optimized
 * for web servers, where we want faster speed and lower lock contention at
 * the expense of space.
 */

/*
 * The attribute allocation strategy is static allocation:
 *   Attributes are pre-allocated as part of the session object and used from
 *   the object array.
 */
#define MAX_OBJS_ATTRS 45 /* number of attributes to preallocate in \
                           * the object (must me the absolute max) */
#define ATTR_SPACE 50     /* Maximum size of attribute data before extra \
                           * data needs to be allocated. This is set to  \
                           * enough space to hold an SSL MASTER secret */

#define NSC_STRICT PR_FALSE /* forces the code to do strict template     \
                             * matching when doing C_FindObject on token \
                             * objects. This will slow down search in    \
                             * NSS. */
/* default search block allocations and increments */
#define NSC_CERT_BLOCK_SIZE 50
#define NSC_SEARCH_BLOCK_SIZE 5
#define NSC_SLOT_LIST_BLOCK_SIZE 10

#define NSC_MIN_SESSION_OBJECT_HANDLE 1U

#define NSC_FIPS_MODULE 1
#define NSC_NON_FIPS_MODULE 0

/* these are data base storage hashes, not cryptographic hashes.. The define
 * the effective size of the various object hash tables */
/* clients care more about memory usage than lookup performance on
 * cyrptographic objects. Clients also have less objects around to play with
 *
 * we eventually should make this configurable at runtime! Especially now that
 * NSS is a shared library.
 */
#define SPACE_ATTRIBUTE_HASH_SIZE 32
#define SPACE_SESSION_OBJECT_HASH_SIZE 32
#define SPACE_SESSION_HASH_SIZE 32
#define TIME_ATTRIBUTE_HASH_SIZE 32
#define TIME_SESSION_OBJECT_HASH_SIZE 1024
#define TIME_SESSION_HASH_SIZE 1024
#define MAX_OBJECT_LIST_SIZE 800
/* how many objects to keep on the free list
 * before we start freeing them */
#define MAX_KEY_LEN 256 /* maximum symmetric key length in bytes */

/*
 * LOG2_BUCKETS_PER_SESSION_LOCK selects how many adjacent head buckets
 * share a single lock. Valid values are in [0, log2(SESSION_HASH_SIZE)].
 *
 * LOG2=0 gives one lock per bucket: no coincidental contention between
 * unrelated handles at the cost of more PRLocks per slot. Larger values
 * trade lock count for contention between sessions whose handles hash
 * into the same lock group.
 *
 * HASH_SIZE   LOG2   BUCKETS_PER_LOCK   NUMBER_OF_LOCKS
 * 32          0      1                  32
 * 32          1      2                  16
 * 32          2      4                  8
 * 32          3      8                  4
 * 1024        0      1                  1024
 * 1024        1      2                  512
 * 1024        5      32                 32
 * 1024        9      512                2
 */
#define LOG2_BUCKETS_PER_SESSION_LOCK 0
#define BUCKETS_PER_SESSION_LOCK (1 << (LOG2_BUCKETS_PER_SESSION_LOCK))

/* define typedefs, double as forward declarations as well */
typedef struct SFTKAttributeStr SFTKAttribute;
typedef struct SFTKObjectListStr SFTKObjectList;
typedef struct SFTKObjectFreeListStr SFTKObjectFreeList;
typedef struct SFTKObjectListElementStr SFTKObjectListElement;
typedef struct SFTKObjectStr SFTKObject;
typedef struct SFTKSessionObjectStr SFTKSessionObject;
typedef struct SFTKTokenObjectStr SFTKTokenObject;
typedef struct SFTKSessionStr SFTKSession;
typedef struct SFTKSlotStr SFTKSlot;
typedef struct SFTKSessionContextStr SFTKSessionContext;
typedef struct SFTKSearchResultsStr SFTKSearchResults;
typedef struct SFTKHashVerifyInfoStr SFTKHashVerifyInfo;
typedef struct SFTKHashSignInfoStr SFTKHashSignInfo;
typedef struct SFTKOAEPInfoStr SFTKOAEPInfo;
typedef struct SFTKPSSSignInfoStr SFTKPSSSignInfo;
typedef struct SFTKPSSVerifyInfoStr SFTKPSSVerifyInfo;
typedef struct SFTKSSLMACInfoStr SFTKSSLMACInfo;
typedef struct SFTKChaCha20Poly1305InfoStr SFTKChaCha20Poly1305Info;
typedef struct SFTKChaCha20CtrInfoStr SFTKChaCha20CtrInfo;
typedef struct SFTKItemTemplateStr SFTKItemTemplate;

/* define function pointer typdefs for pointer tables */
typedef void (*SFTKDestroy)(void *, PRBool);
typedef void (*SFTKBegin)(void *);
typedef SECStatus (*SFTKCipher)(void *, unsigned char *, unsigned int *, unsigned int,
                                const unsigned char *, unsigned int);
typedef SECStatus (*SFTKAEADCipher)(void *, void *, unsigned int *,
                                    unsigned int, void *, unsigned int,
                                    void *, unsigned int, void *, unsigned int);
typedef SECStatus (*SFTKVerify)(void *, const unsigned char *, unsigned int, const unsigned char *, unsigned int);
typedef void (*SFTKHash)(void *, const unsigned char *, unsigned int);
typedef void (*SFTKEnd)(void *, unsigned char *, unsigned int *, unsigned int);
typedef void (*SFTKFree)(void *);

/* Value to tell if an attribute is modifiable or not.
 *    NEVER: attribute is only set on creation.
 *    ONCOPY: attribute is set on creation and can only be changed on copy.
 *    SENSITIVE: attribute can only be changed to TRUE.
 *    ALWAYS: attribute can always be changed.
 */
typedef enum {
    SFTK_NEVER = 0,
    SFTK_ONCOPY = 1,
    SFTK_SENSITIVE = 2,
    SFTK_ALWAYS = 3
} SFTKModifyType;

/*
 * Free Status Enum... tell us more information when we think we're
 * deleting an object.
 */
typedef enum {
    SFTK_DestroyFailure,
    SFTK_Destroyed,
    SFTK_Busy
} SFTKFreeStatus;

/*
 * Source of various objects
 */
typedef enum {
    SFTK_SOURCE_DEFAULT = 0,
    SFTK_SOURCE_KEA,
    SFTK_SOURCE_HKDF_EXPAND,
    SFTK_SOURCE_HKDF_EXTRACT
} SFTKSource;

/*
 * attribute values of an object.
 */
struct SFTKAttributeStr {
    SFTKAttribute *next;
    SFTKAttribute *prev;
    PRBool freeAttr;
    PRBool freeData;
    /*must be called handle to make sftkqueue_find work */
    CK_ATTRIBUTE_TYPE handle;
    CK_ATTRIBUTE attrib;
    unsigned char space[ATTR_SPACE];
};

/*
 * doubly link list of objects
 */
struct SFTKObjectListStr {
    SFTKObjectList *next;
    SFTKObjectList *prev;
    SFTKObject *parent;
};

struct SFTKObjectFreeListStr {
    SFTKObject *head;
    PRLock *lock;
    int count;
};

/*
 * PKCS 11 crypto object structure
 */
struct SFTKObjectStr {
    SFTKObject *next;
    SFTKObject *prev;
    CK_OBJECT_CLASS objclass;
    CK_OBJECT_HANDLE handle;
    int refCount;
    PRUint32 type; /* SFTK_SESSION_OBJECT_TYPE or SFTK_TOKEN_OBJECT_TYPE */
    PRLock *refLock;
    SFTKSlot *slot;
    void *objectInfo;
    SFTKFree infoFree;
    CK_FLAGS validation_value;
    SFTKAttribute validation_attribute;
    SFTKSource source;
};

struct SFTKTokenObjectStr {
    SFTKObject obj;
    SECItem dbKey;
};

struct SFTKSessionObjectStr {
    SFTKObject obj;
    SFTKObjectList sessionList;
    PRLock *attributeLock;
    SFTKSession *session;
    PRBool wasDerived;
    int nextAttr;
    SFTKAttribute attrList[MAX_OBJS_ATTRS];
    PRBool optimizeSpace;
    unsigned int hashSize;
    SFTKAttribute *head[1];
};

/*
 * struct to deal with a temparary list of objects
 */
struct SFTKObjectListElementStr {
    SFTKObjectListElement *next;
    SFTKObject *object;
};

/*
 * Area to hold Search results
 */
struct SFTKSearchResultsStr {
    CK_OBJECT_HANDLE *handles;
    int size;
    int index;
    int array_size;
};

/*
 * the universal crypto/hash/sign/verify context structure
 */
typedef enum {
    SFTK_ENCRYPT,
    SFTK_DECRYPT,
    SFTK_HASH,
    SFTK_SIGN,
    SFTK_SIGN_RECOVER,
    SFTK_VERIFY,
    SFTK_VERIFY_RECOVER,
    SFTK_MESSAGE_ENCRYPT,
    SFTK_MESSAGE_DECRYPT,
    SFTK_MESSAGE_SIGN,
    SFTK_MESSAGE_VERIFY
} SFTKContextType;

/** max block size of supported block ciphers */
#define SFTK_MAX_BLOCK_SIZE 16
/** currently SHA512 is the biggest hash length */
#define SFTK_MAX_MAC_LENGTH 64
#define SFTK_INVALID_MAC_SIZE 0xffffffff

/** Particular ongoing operation in session (sign/verify/digest/encrypt/...)
 *
 *  Understanding sign/verify context:
 *      multi=1 hashInfo=0   block (symmetric) cipher MACing
 *      multi=1 hashInfo=X   PKC S/V with prior hashing
 *      multi=0 hashInfo=0   PKC S/V one shot (w/o hashing)
 *      multi=0 hashInfo=X   *** shouldn't happen ***
 */
struct SFTKSessionContextStr {
    SFTKContextType type;
    PRBool multi;               /* is multipart */
    PRBool rsa;                 /* is rsa */
    PRBool doPad;               /* use PKCS padding for block ciphers */
    PRBool isXCBC;              /* xcbc, use special handling in final */
    PRBool isFIPS;              /* current operation is in FIPS mode */
    unsigned int blockSize;     /* blocksize for padding */
    unsigned int padDataLength; /* length of the valid data in padbuf */
    /** latest incomplete block of data for block cipher */
    unsigned char padBuf[SFTK_MAX_BLOCK_SIZE];
    /** result of MAC'ing of latest full block of data with block cipher */
    unsigned char macBuf[SFTK_MAX_BLOCK_SIZE];
    unsigned char k2[SFTK_MAX_BLOCK_SIZE];
    unsigned char k3[SFTK_MAX_BLOCK_SIZE];
    CK_ULONG macSize; /* size of a general block cipher mac*/
    void *cipherInfo;
    void *hashInfo;
    unsigned int cipherInfoLen;
    CK_MECHANISM_TYPE currentMech;
    SFTKCipher update;
    SFTKAEADCipher aeadUpdate;
    SFTKHash hashUpdate;
    SFTKEnd end;
    SFTKDestroy destroy;
    SFTKDestroy hashdestroy;
    SFTKVerify verify;
    unsigned int maxLen;
    SFTKObject *key;
    SECItem *signature;
};

/*
 * Sessions (have objects)
 */
struct SFTKSessionStr {
    SFTKSession *next;
    SFTKSession *prev;
    CK_SESSION_HANDLE handle;
    int refCount; /* protected by SFTK_SESSION_LOCK(slot, handle) */
    int objectIDCount;
    CK_SESSION_INFO info;
    CK_NOTIFY notify;
    CK_VOID_PTR appData;
    SFTKSlot *slot;
    SFTKSearchResults *search;
    SFTKSessionContext *enc_context;
    SFTKSessionContext *hash_context;
    PRBool lastOpWasFIPS;
    /* protected by slot->objectLock, see below */
    SFTKObjectList *objects[1];
};

/*
 * slots (have sessions and objects)
 *
 * The array of sessionLock's protect the session hash table (head[])
 * as well as the reference count of session objects in that bucket
 * (head[]->refCount),  objectLock protects all elements of the slot's
 * object hash tables (sessObjHashTable[] and tokObjHashTable),
 * sessionObjectHandleCount, and the per-session object lists
 * (SFTKSession.objects[] and SFTKSessionObject.sessionList/session) of
 * every session on this slot.
 *
 * The per-session object lists deliberately do NOT have a per-session lock.
 * Object handles are slot-global, so sftk_DeleteObject() can be asked to
 * unlink an object from a session other than the caller's, on which it holds
 * no reference; a lock owned by that session could be destroyed underneath
 * it (bug 2061391). Using the slot's lock -- which outlives every session on
 * the slot -- also keeps the two queues atomic with respect to each other,
 * giving the invariant that a session object is on its owning session's list
 * if and only if it is in sessObjHashTable[].
 *
 * slotLock protects password, needLogin, isLoggedIn, ssoLoggedIn,
 * sessionCount, and rwSessionCount.
 * pwCheckLock serializes the key database password checks in
 * NSC_SetPIN and NSC_Login.
 *
 * Each of the fields below has the following lifetime as commented
 * next to the fields:
 *   invariant  - This value is set when the slot is first created and
 * never changed until it is destroyed.
 *   per load   - This value is set when the slot is first created, or
 * when the slot is used to open another directory. Between open and close
 * this field does not change.
 *   variable - This value changes through the normal process of slot operation.
 *      - reset. The value of this variable is cleared during an open/close
 *   cycles.
 *      - preserved. The value of this variable is preserved over open/close
 *   cycles.
 */
struct SFTKSlotStr {
    CK_SLOT_ID slotID;             /* invariant */
    PRLock *slotLock;              /* invariant */
    PRLock **sessionLock;          /* invariant */
    unsigned int numSessionLocks;  /* invariant */
    unsigned long sessionLockMask; /* invariant */
    PRLock *objectLock;            /* invariant */
    PRLock *pwCheckLock;           /* invariant */
    PRBool present;                /* variable -set */
    PRBool hasTokens;              /* per load */
    PRBool isLoggedIn;             /* variable - reset */
    PRBool ssoLoggedIn;            /* variable - reset */
    PRBool needLogin;              /* per load */
    PRBool DB_loaded;              /* per load */
    PRBool readOnly;               /* per load */
    PRBool optimizeSpace;          /* invariant */
    SFTKDBHandle *certDB;          /* per load */
    SFTKDBHandle *keyDB;           /* per load */
    int minimumPinLen;             /* per load */
    PRInt32 sessionIDCount;        /* atomically incremented */
                                   /* (preserved) */
    int sessionIDConflict;         /* not protected by a lock */
                                   /* (preserved) */
    int sessionCount;              /* variable - reset */
    int rwSessionCount;            /* variable - reset */
    int sessionObjectHandleCount;  /* variable - perserved */
    CK_ULONG index;                /* invariant */
    PLHashTable *tokObjHashTable;  /* invariant */
    SFTKObject **sessObjHashTable; /* variable - reset */
    unsigned int sessObjHashSize;  /* invariant */
    SFTKSession **head;            /* variable -reset */
    unsigned int sessHashSize;     /* invariant */
    char tokDescription[33];       /* per load */
    char updateTokDescription[33]; /* per load */
    char slotDescription[65];      /* invariant */
    SFTKSession moduleObjects;     /* global session to hang module specific
                                    * objects like profile objects or
                                    * validation objects */
};

/*
 * special joint operations Contexts
 */
struct SFTKHashVerifyInfoStr {
    SECOidTag hashOid;
    void *params;
    NSSLOWKEYPublicKey *key;
};

struct SFTKHashSignInfoStr {
    SECOidTag hashOid;
    void *params;
    NSSLOWKEYPrivateKey *key;
};

struct SFTKPSSVerifyInfoStr {
    size_t size; /* must be first */
    CK_RSA_PKCS_PSS_PARAMS params;
    NSSLOWKEYPublicKey *key;
};

struct SFTKPSSSignInfoStr {
    size_t size; /* must be first */
    CK_RSA_PKCS_PSS_PARAMS params;
    NSSLOWKEYPrivateKey *key;
};

/**
 * Contexts for RSA-OAEP
 */
struct SFTKOAEPInfoStr {
    CK_RSA_PKCS_OAEP_PARAMS params;
    PRBool isEncrypt;
    union {
        NSSLOWKEYPublicKey *pub;
        NSSLOWKEYPrivateKey *priv;
    } key;
};

/* context for the Final SSLMAC message */
struct SFTKSSLMACInfoStr {
    size_t size; /* must be first */
    void *hashContext;
    SFTKBegin begin;
    SFTKHash update;
    SFTKEnd end;
    CK_ULONG macSize;
    int padSize;
    unsigned char key[MAX_KEY_LEN];
    unsigned int keySize;
};

/* SFTKChaCha20Poly1305Info saves the key, tag length, nonce,
 * and additional data for a ChaCha20+Poly1305 AEAD operation. */
struct SFTKChaCha20Poly1305InfoStr {
    ChaCha20Poly1305Context freeblCtx;
    unsigned char nonce[12];
    unsigned char ad[16];
    unsigned char *adOverflow;
    unsigned int adLen;
};

/* SFTKChaCha20BlockInfoStr the key, nonce and counter for a
 * ChaCha20 block operation. */
struct SFTKChaCha20CtrInfoStr {
    PRUint8 key[32];
    PRUint8 nonce[12];
    PRUint32 counter;
};

/*
 * Template based on SECItems, suitable for passing as arrays
 */
struct SFTKItemTemplateStr {
    CK_ATTRIBUTE_TYPE type;
    SECItem *item;
};

/* macro for setting SFTKTemplates. */
#define SFTK_SET_ITEM_TEMPLATE(templ, count, itemPtr, attr) \
    templ[count].type = attr;                               \
    templ[count].item = itemPtr

#define SFTK_MAX_ITEM_TEMPLATE 10

/*
 * session handle modifiers
 */
#define SFTK_SESSION_SLOT_MASK 0xff000000L

/*
 * object handle modifiers
 */
#define SFTK_TOKEN_MASK 0x80000000L
#define SFTK_TOKEN_MAGIC 0x80000000L
#define SFTK_TOKEN_TYPE_MASK 0x70000000L
/* keydb (high bit == 0) */
#define SFTK_TOKEN_TYPE_PRIV 0x10000000L
#define SFTK_TOKEN_TYPE_PUB 0x20000000L
#define SFTK_TOKEN_TYPE_KEY 0x30000000L
/* certdb (high bit == 1) */
#define SFTK_TOKEN_TYPE_TRUST 0x40000000L
#define SFTK_TOKEN_TYPE_CRL 0x50000000L
#define SFTK_TOKEN_TYPE_SMIME 0x60000000L
#define SFTK_TOKEN_TYPE_CERT 0x70000000L

#define SFTK_TOKEN_KRL_HANDLE (SFTK_TOKEN_MAGIC | SFTK_TOKEN_TYPE_CRL | 1)
/* how big (in bytes) a password/pin we can deal with */
#define SFTK_MAX_PIN 500
/* minimum password/pin length (in Unicode characters) in FIPS mode */
#define FIPS_MIN_PIN 7

/* slot ID's */
#define NETSCAPE_SLOT_ID 1
#define PRIVATE_KEY_SLOT_ID 2
#define FIPS_SLOT_ID 3

/* slot helper macros */
#define sftk_SlotFromSession(sp) ((sp)->slot)
#define sftk_isToken(id) (((id) & SFTK_TOKEN_MASK) == SFTK_TOKEN_MAGIC)

/* Type tag values for SFTKObject.type */
#define SFTK_SESSION_OBJECT_TYPE 0xFFFFFFFFU
#define SFTK_TOKEN_OBJECT_TYPE 0x00000000U

#define sftk_isFIPS(id) \
    (((id) == FIPS_SLOT_ID) || ((id) >= SFTK_MIN_FIPS_USER_SLOT_ID))

/* validation flags. These are token specific, but also
 * visible to the application via the validation object
 * each validation should cover a unique bit. */
#define SFTK_VALIDATION_FIPS_FLAG 0x00000001L

/* the session hash multiplier (see bug 201081) */
#define SHMULTIPLIER 1791398085

/* queueing helper macros */
#define sftk_hash(value, size) \
    ((PRUint32)((value) * SHMULTIPLIER) & (size - 1))
#define sftkqueue_add(element, id, head, hash_size) \
    {                                               \
        int tmp = sftk_hash(id, hash_size);         \
        (element)->next = (head)[tmp];              \
        (element)->prev = NULL;                     \
        if ((head)[tmp])                            \
            (head)[tmp]->prev = (element);          \
        (head)[tmp] = (element);                    \
    }
#define sftkqueue_find(element, id, head, hash_size)                      \
    for ((element) = (head)[sftk_hash(id, hash_size)]; (element) != NULL; \
         (element) = (element)->next) {                                   \
        if ((element)->handle == (id)) {                                  \
            break;                                                        \
        }                                                                 \
    }
#define sftkqueue_is_queued(element, id, head, hash_size) \
    (((element)->next) || ((element)->prev) ||            \
     ((head)[sftk_hash(id, hash_size)] == (element)))
#define sftkqueue_check_links(element, bucket_head)                 \
    do {                                                            \
        PORT_ReleaseAssert(!(element)->next ||                      \
                           (element)->next->prev == (element));     \
        PORT_ReleaseAssert((element)->prev                          \
                               ? (element)->prev->next == (element) \
                               : (bucket_head) == (element));       \
    } while (0)
#define sftkqueue_delete(element, id, head, hash_size)       \
    do {                                                     \
        PRUint32 sftkq_index = sftk_hash(id, hash_size);     \
        sftkqueue_check_links(element, (head)[sftkq_index]); \
        if ((element)->next)                                 \
            (element)->next->prev = (element)->prev;         \
        if ((element)->prev)                                 \
            (element)->prev->next = (element)->next;         \
        else                                                 \
            (head)[sftkq_index] = ((element)->next);         \
        (element)->next = NULL;                              \
        (element)->prev = NULL;                              \
    } while (0)

#define sftkqueue_init_element(element) \
    (element)->prev = NULL;

#define sftkqueue_add2(element, id, index, head) \
    {                                            \
        (element)->next = (head)[index];         \
        if ((head)[index])                       \
            (head)[index]->prev = (element);     \
        (head)[index] = (element);               \
    }

#define sftkqueue_find2(element, id, index, head) \
    for ((element) = (head)[index];               \
         (element) != NULL;                       \
         (element) = (element)->next) {           \
        if ((element)->handle == (id)) {          \
            break;                                \
        }                                         \
    }

#define sftkqueue_delete2(element, id, index, head)    \
    do {                                               \
        sftkqueue_check_links(element, (head)[index]); \
        if ((element)->next)                           \
            (element)->next->prev = (element)->prev;   \
        if ((element)->prev)                           \
            (element)->prev->next = (element)->next;   \
        else                                           \
            (head)[index] = ((element)->next);         \
    } while (0)

#define sftkqueue_clear_deleted_element(element) \
    (element)->next = NULL;                      \
    (element)->prev = NULL;

/* The session lock for a head bucket protects every session linked into
 * that bucket as well as the head pointer itself. Each lock covers
 * BUCKETS_PER_SESSION_LOCK adjacent head buckets.
 *
 * Both forms below derive the lock from the head-bucket index, so that
 * SFTK_SESSION_LOCK(slot, handle) and SFTK_HEAD_BUCKET_LOCK(slot,
 * sftk_hash(handle, sessHashSize)) name the same lock. Walkers like
 * sftk_CloseAllSessions and sftk_update_all_states iterate by bucket
 * index, so they must use SFTK_HEAD_BUCKET_LOCK directly. */
#define SFTK_HEAD_BUCKET_LOCK(slot, bucket) \
    ((slot)->sessionLock[((bucket) >> LOG2_BUCKETS_PER_SESSION_LOCK) & (slot)->sessionLockMask])
#define SFTK_SESSION_LOCK(slot, handle) \
    SFTK_HEAD_BUCKET_LOCK(slot, sftk_hash((handle), (slot)->sessHashSize))

/* expand an attribute & secitem structures out */
#define sftk_attr_expand(ap) (ap)->type, (ap)->pValue, (ap)->ulValueLen
#define sftk_item_expand(ip) (ip)->data, (ip)->len

typedef struct sftk_token_parametersStr {
    CK_SLOT_ID slotID;
    char *configdir;
    char *certPrefix;
    char *keyPrefix;
    char *updatedir;
    char *updCertPrefix;
    char *updKeyPrefix;
    char *updateID;
    char *tokdes;
    char *slotdes;
    char *updtokdes;
    int minPW;
    PRBool readOnly;
    PRBool noCertDB;
    PRBool noKeyDB;
    PRBool forceOpen;
    PRBool pwRequired;
    PRBool optimizeSpace;
} sftk_token_parameters;

typedef struct sftk_parametersStr {
    char *configdir;
    char *updatedir;
    char *updateID;
    char *secmodName;
    char *man;
    char *libdes;
    PRBool readOnly;
    PRBool noModDB;
    PRBool noCertDB;
    PRBool forceOpen;
    PRBool pwRequired;
    PRBool optimizeSpace;
    sftk_token_parameters *tokens;
    int token_count;
} sftk_parameters;

/* path stuff (was machine dependent) used by dbinit.c and pk11db.c */
#define CERT_DB_FMT "%scert%s.db"
#define KEY_DB_FMT "%skey%s.db"

struct sftk_MACConstantTimeCtxStr {
    const SECHashObject *hash;
    unsigned char mac[64];
    unsigned char secret[64];
    unsigned int headerLength;
    unsigned int secretLength;
    unsigned int totalLength;
    unsigned char header[75];
};
typedef struct sftk_MACConstantTimeCtxStr sftk_MACConstantTimeCtx;

struct sftk_MACCtxStr {
    /* This is a common MAC context that supports both HMAC and CMAC
     * operations. This also presents a unified set of semantics:
     *
     *  - Everything except Destroy returns a CK_RV, indicating success
     *    or failure. (This handles the difference between HMAC's and CMAC's
     *    interfaces, since the underlying AES _might_ fail with CMAC).
     *
     *  - The underlying MAC is started on Init(...), so Update(...) can
     *    called right away. (This handles the difference between HMAC and
     *    CMAC in their *_Init(...) functions).
     *
     *  - Calling semantics:
     *
     *      - One of sftk_MAC_{Create,Init,InitRaw}(...) to set up the MAC
     *        context, checking the return code.
     *      - sftk_MAC_Update(...) as many times as necessary to process
     *        input data, checking the return code.
     *      - sftk_MAC_End(...) to get the output of the MAC; result_len
     *        may be NULL if the caller knows the expected output length,
     *        checking the return code. If result_len is NULL, this will
     *        PR_ASSERT(...) that the actual returned length was equal to
     *        max_result_len.
     *
     *        Note: unlike HMAC_Finish(...), this allows the caller to specify
     *        a return value less than return length, to align with
     *        CMAC_Finish(...)'s semantics. This will force an additional
     *        stack allocation of size SFTK_MAX_MAC_LENGTH.
     *      - sftk_MAC_Reset(...) if the caller wishes to compute a new MAC
     *        with the same key, checking the return code.
     *      - sftk_MAC_DestroyContext(...) when the caller frees its associated
     *        memory, passing PR_TRUE if sftk_MAC_Create(...) was called,
     *        and PR_FALSE otherwise.
     */

    CK_MECHANISM_TYPE mech;
    unsigned int mac_size;

    union {
        HMACContext *hmac;
        CMACContext *cmac;

        /* Functions to update when adding a new MAC or a new hash:
         *
         *  - sftk_MAC_Init
         *  - sftk_MAC_Update
         *  - sftk_MAC_End
         *  - sftk_MAC_Reset
         */
        void *raw;
    } mac;

    void (*destroy_func)(void *ctx, PRBool free_it);
};
typedef struct sftk_MACCtxStr sftk_MACCtx;

extern CK_NSS_MODULE_FUNCTIONS sftk_module_funcList;
extern CK_NSS_FIPS_FUNCTIONS sftk_fips_funcList;

SEC_BEGIN_PROTOS

/* shared functions between pkcs11.c and fipstokn.c */
extern PRBool nsf_init;
extern CK_RV nsc_CommonInitialize(CK_VOID_PTR pReserved, PRBool isFIPS);
extern CK_RV nsc_CommonFinalize(CK_VOID_PTR pReserved, PRBool isFIPS);
extern PRBool sftk_ForkReset(CK_VOID_PTR pReserved, CK_RV *crv);
extern CK_RV nsc_CommonGetSlotList(CK_BBOOL tokPresent,
                                   CK_SLOT_ID_PTR pSlotList,
                                   CK_ULONG_PTR pulCount,
                                   unsigned int moduleIndex);

/* slot initialization, reinit, shutdown and destruction */
extern CK_RV SFTK_SlotInit(char *configdir, char *updatedir, char *updateID,
                           sftk_token_parameters *params,
                           unsigned int moduleIndex);
extern CK_RV SFTK_SlotReInit(SFTKSlot *slot, char *configdir,
                             char *updatedir, char *updateID,
                             sftk_token_parameters *params,
                             unsigned int moduleIndex);
extern CK_RV SFTK_DestroySlotData(SFTKSlot *slot);
extern CK_RV SFTK_ShutdownSlot(SFTKSlot *slot);
extern CK_RV sftk_CloseAllSessions(SFTKSlot *slot, PRBool logout);

/* internal utility functions used by pkcs11.c */
extern CK_RV sftk_MapCryptError(int error);
extern CK_RV sftk_MapDecryptError(int error);
extern CK_RV sftk_MapVerifyError(int error);
extern SFTKAttribute *sftk_FindAttribute(SFTKObject *object,
                                         CK_ATTRIBUTE_TYPE type);
extern void sftk_FreeAttribute(SFTKAttribute *attribute);
extern CK_RV sftk_AddAttributeType(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                                   const void *valPtr, CK_ULONG length);
extern CK_RV sftk_Attribute2SecItem(PLArenaPool *arena, SECItem *item,
                                    SFTKObject *object, CK_ATTRIBUTE_TYPE type);
extern CK_RV sftk_MultipleAttribute2SecItem(PLArenaPool *arena,
                                            SFTKObject *object,
                                            SFTKItemTemplate *templ, int count);
extern unsigned int sftk_GetLengthInBits(unsigned char *buf,
                                         unsigned int bufLen);
extern CK_RV sftk_ConstrainAttribute(SFTKObject *object,
                                     CK_ATTRIBUTE_TYPE type, int minLength,
                                     int maxLength, int minMultiple);
extern PRBool sftk_hasAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type);
extern PRBool sftk_isTrue(SFTKObject *object, CK_ATTRIBUTE_TYPE type);
extern void sftk_DeleteAttributeType(SFTKObject *object,
                                     CK_ATTRIBUTE_TYPE type);
extern CK_RV sftk_Attribute2SecItem(PLArenaPool *arena, SECItem *item,
                                    SFTKObject *object, CK_ATTRIBUTE_TYPE type);
extern CK_RV sftk_Attribute2SSecItem(PLArenaPool *arena, SECItem *item,
                                     SFTKObject *object,
                                     CK_ATTRIBUTE_TYPE type);
extern SFTKModifyType sftk_modifyType(CK_ATTRIBUTE_TYPE type,
                                      CK_OBJECT_CLASS inClass);
extern PRBool sftk_isSensitive(CK_ATTRIBUTE_TYPE type, CK_OBJECT_CLASS inClass);
extern CK_RV sftk_GetULongAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                                    CK_ULONG *longData);
extern CK_RV sftk_ReadAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                                unsigned char *data, unsigned int maxlen,
                                unsigned int *lenp);
extern CK_RV sftk_forceAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                                 const void *value, unsigned int len);
extern CK_RV sftk_defaultAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                                   const void *value, unsigned int len);
extern unsigned int sftk_MapTrust(CK_TRUST trust, PRBool clientAuth);

extern SFTKObject *sftk_NewObject(SFTKSlot *slot);
extern CK_RV sftk_CopyObject(SFTKObject *destObject, SFTKObject *srcObject);
extern SFTKFreeStatus sftk_FreeObject(SFTKObject *object);
extern CK_RV sftk_DeleteObject(SFTKSession *session, SFTKObject *object);
extern void sftk_ReferenceObject(SFTKObject *object);
extern SFTKObject *sftk_ObjectFromHandle(CK_OBJECT_HANDLE handle,
                                         SFTKSession *session);
extern CK_OBJECT_HANDLE sftk_getNextHandle(SFTKSlot *slot);
extern void sftk_AddObject(SFTKSession *session, SFTKObject *object);
/* clear out all the existing object ID to database key mappings.
 * used to reinit a token */
extern CK_RV SFTK_ClearTokenKeyHashTable(SFTKSlot *slot);

extern CK_RV sftk_searchObjectList(SFTKSearchResults *search,
                                   SFTKObject **head, unsigned int size,
                                   PRLock *lock, CK_ATTRIBUTE_PTR inTemplate,
                                   int count, PRBool isLoggedIn);
extern SFTKObjectListElement *sftk_FreeObjectListElement(
    SFTKObjectListElement *objectList);
extern void sftk_FreeObjectList(SFTKObjectListElement *objectList);
extern void sftk_FreeSearch(SFTKSearchResults *search);
extern CK_RV sftk_handleObject(SFTKObject *object, SFTKSession *session);

extern SFTKSlot *sftk_SlotFromID(CK_SLOT_ID slotID, PRBool all);
extern SFTKSlot *sftk_SlotFromSessionHandle(CK_SESSION_HANDLE handle);
extern CK_SLOT_ID sftk_SlotIDFromSessionHandle(CK_SESSION_HANDLE handle);
extern SFTKSession *sftk_SessionFromHandle(CK_SESSION_HANDLE handle);
extern void sftk_FreeSession(SFTKSession *session);
extern void sftk_ClearSession(SFTKSession *session);
extern CK_RV sftk_InitSession(SFTKSession *session, SFTKSlot *slot,
                              CK_SLOT_ID slotID, CK_NOTIFY notify,
                              CK_VOID_PTR pApplication, CK_FLAGS flags);
extern SFTKSession *sftk_NewSession(CK_SLOT_ID slotID, CK_NOTIFY notify,
                                    CK_VOID_PTR pApplication, CK_FLAGS flags);
extern void sftk_update_state(SFTKSlot *slot, SFTKSession *session);
extern void sftk_update_all_states(SFTKSlot *slot);
extern void sftk_InitFreeLists(void);
extern void sftk_CleanupFreeLists(void);

/*
 * Helper functions to handle the session crypto contexts
 */
extern CK_RV sftk_InitGeneric(SFTKSession *session,
                              CK_MECHANISM *pMechanism,
                              SFTKSessionContext **contextPtr,
                              SFTKContextType ctype, SFTKObject **keyPtr,
                              CK_OBJECT_HANDLE hKey, CK_KEY_TYPE *keyTypePtr,
                              CK_OBJECT_CLASS pubKeyType,
                              CK_ATTRIBUTE_TYPE operation);
void sftk_SetContextByType(SFTKSession *session, SFTKContextType type,
                           SFTKSessionContext *context);
extern CK_RV sftk_InstallContext(SFTKSession *session, SFTKContextType type,
                                 SFTKSessionContext *context);
extern void sftk_UninstallContext(SFTKSession *session, SFTKContextType type);
extern CK_RV sftk_GetContext(CK_SESSION_HANDLE handle,
                             SFTKSessionContext **contextPtr,
                             SFTKContextType type, PRBool needMulti,
                             SFTKSession **sessionPtr);
extern void sftk_TerminateOp(SFTKSession *session, SFTKContextType ctype);
extern void sftk_FreeContext(SFTKSessionContext *context);

extern NSSLOWKEYPublicKey *sftk_GetPubKey(SFTKObject *object,
                                          CK_KEY_TYPE key_type, CK_RV *crvp);
extern NSSLOWKEYPrivateKey *sftk_GetPrivKey(SFTKObject *object,
                                            CK_KEY_TYPE key_type, CK_RV *crvp);
extern CK_RV sftk_PutPubKey(SFTKObject *publicKey, SFTKObject *privKey, CK_KEY_TYPE keyType,
                            NSSLOWKEYPublicKey *pubKey);
extern void sftk_FormatDESKey(unsigned char *key, int length);
extern PRBool sftk_CheckDESKey(unsigned char *key);
extern PRBool sftk_IsWeakKey(unsigned char *key, CK_KEY_TYPE key_type);
extern void sftk_EncodeInteger(PRUint64 integer, CK_ULONG num_bits, CK_BBOOL littleEndian,
                               CK_BYTE_PTR output, CK_ULONG_PTR output_len);

/* ike and xcbc helpers */
extern CK_RV sftk_ike_prf(CK_SESSION_HANDLE hSession,
                          const SFTKAttribute *inKey,
                          const CK_IKE_PRF_DERIVE_PARAMS *params, SFTKObject *outKey);
extern CK_RV sftk_ike1_prf(CK_SESSION_HANDLE hSession,
                           const SFTKAttribute *inKey,
                           const CK_IKE1_PRF_DERIVE_PARAMS *params, SFTKObject *outKey,
                           unsigned int keySize);
extern CK_RV sftk_ike1_appendix_b_prf(CK_SESSION_HANDLE hSession,
                                      const SFTKAttribute *inKey,
                                      const CK_IKE1_EXTENDED_DERIVE_PARAMS *params,
                                      SFTKObject *outKey,
                                      unsigned int keySize);
extern CK_RV sftk_ike_prf_plus(CK_SESSION_HANDLE hSession,
                               const SFTKAttribute *inKey,
                               const CK_IKE2_PRF_PLUS_DERIVE_PARAMS *params, SFTKObject *outKey,
                               unsigned int keySize);
extern CK_RV sftk_aes_xcbc_new_keys(CK_SESSION_HANDLE hSession,
                                    CK_OBJECT_HANDLE hKey, CK_OBJECT_HANDLE_PTR phKey,
                                    unsigned char *k2, unsigned char *k3);
extern CK_RV sftk_xcbc_mac_pad(unsigned char *padBuf, unsigned int bufLen,
                               unsigned int blockSize, const unsigned char *k2,
                               const unsigned char *k3);
extern SECStatus sftk_fips_IKE_PowerUpSelfTests(void);

/* mechanism allows this operation */
extern CK_RV sftk_MechAllowsOperation(CK_MECHANISM_TYPE type, CK_ATTRIBUTE_TYPE op);

/* helper function which calls nsslowkey_FindKeyByPublicKey after safely
 * acquiring a reference to the keydb from the slot */
NSSLOWKEYPrivateKey *sftk_FindKeyByPublicKey(SFTKSlot *slot, SECItem *dbKey);

/*
 * parameter parsing functions
 */
CK_RV sftk_parseParameters(char *param, sftk_parameters *parsed, PRBool isFIPS);
void sftk_freeParams(sftk_parameters *params);
PRBool sftk_RawArgHasFlag(const char *entry, const char *flag, const void *pReserved);

/*
 * narrow objects
 */
SFTKSessionObject *sftk_narrowToSessionObject(SFTKObject *);
SFTKTokenObject *sftk_narrowToTokenObject(SFTKObject *);

/*
 * token object utilities
 */
void sftk_addHandle(SFTKSearchResults *search, CK_OBJECT_HANDLE handle);
PRBool sftk_poisonHandle(SFTKSlot *slot, SECItem *dbkey,
                         CK_OBJECT_HANDLE handle);
SFTKObject *sftk_NewTokenObject(SFTKSlot *slot, SECItem *dbKey,
                                CK_OBJECT_HANDLE handle);
CK_RV sftk_convertSessionToToken(SFTKObject *so);

/* J-PAKE (jpakesftk.c) */
extern CK_RV jpake_Round1(HASH_HashType hashType,
                          CK_NSS_JPAKERound1Params *params,
                          SFTKObject *key);
extern CK_RV jpake_Round2(HASH_HashType hashType,
                          CK_NSS_JPAKERound2Params *params,
                          SFTKObject *sourceKey, SFTKObject *key);
extern CK_RV jpake_Final(HASH_HashType hashType,
                         const CK_NSS_JPAKEFinalParams *params,
                         SFTKObject *sourceKey, SFTKObject *key);

/* Constant time MAC functions (hmacct.c) */
sftk_MACConstantTimeCtx *sftk_HMACConstantTime_New(
    CK_MECHANISM_PTR mech, SFTKObject *key);
sftk_MACConstantTimeCtx *sftk_SSLv3MACConstantTime_New(
    CK_MECHANISM_PTR mech, SFTKObject *key);
void sftk_HMACConstantTime_Update(void *pctx, const unsigned char *data, unsigned int len);
void sftk_SSLv3MACConstantTime_Update(void *pctx, const unsigned char *data, unsigned int len);
void sftk_MACConstantTime_EndHash(
    void *pctx, unsigned char *out, unsigned int *outLength, unsigned int maxLength);
void sftk_MACConstantTime_DestroyContext(void *pctx, PRBool);

/* Crypto Utilities */
HASH_HashType sftk_GetHashTypeFromMechanism(CK_MECHANISM_TYPE mech);

/****************************************
 * implement TLS Pseudo Random Function (PRF)
 */

extern CK_RV
sftk_TLSPRFInit(SFTKSessionContext *context,
                SFTKObject *key,
                CK_KEY_TYPE key_type,
                HASH_HashType hash_alg,
                unsigned int out_len);

/* PKCS#11 MAC implementation. See sftk_MACCtxStr declaration above for
 * calling semantics for these functions. */
HASH_HashType sftk_HMACMechanismToHash(CK_MECHANISM_TYPE mech);
CK_RV sftk_MAC_Create(CK_MECHANISM_TYPE mech, SFTKObject *key, sftk_MACCtx **ret_ctx);
CK_RV sftk_MAC_Init(sftk_MACCtx *ctx, CK_MECHANISM_TYPE mech, SFTKObject *key);
CK_RV sftk_MAC_InitRaw(sftk_MACCtx *ctx, CK_MECHANISM_TYPE mech, const unsigned char *key, unsigned int key_len, PRBool isFIPS);
CK_RV sftk_MAC_Update(sftk_MACCtx *ctx, const CK_BYTE *data, unsigned int data_len);
CK_RV sftk_MAC_End(sftk_MACCtx *ctx, CK_BYTE_PTR result, unsigned int *result_len, unsigned int max_result_len);
CK_RV sftk_MAC_Reset(sftk_MACCtx *ctx);
void sftk_MAC_DestroyContext(sftk_MACCtx *ctx, PRBool free_it);

/* constant time helpers */
unsigned int sftk_CKRVToMask(CK_RV rv);
CK_RV sftk_CheckCBCPadding(CK_BYTE_PTR pBuf, unsigned int bufLen,
                           unsigned int blockSize, unsigned int *outPadSize);

/* NIST 800-108 (kbkdf.c) implementations */
extern CK_RV kbkdf_Dispatch(CK_MECHANISM_TYPE mech, CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, SFTKObject *base_key, SFTKObject *ret_key, CK_ULONG keySize);
extern SECStatus sftk_fips_SP800_108_PowerUpSelfTests(void);

/* export the HKDF function for use in PowerupSelfTests */
CK_RV sftk_HKDF(CK_HKDF_PARAMS_PTR params, CK_SESSION_HANDLE hSession,
                SFTKObject *sourceKey, const unsigned char *sourceKeyBytes,
                int sourceKeyLen, SFTKObject *key,
                unsigned char *outKeyBytes, int keySize,
                PRBool canBeData, PRBool isFIPS);

char **NSC_ModuleDBFunc(unsigned long function, char *parameters, void *args);

/* dh verify functions */
/* verify that dhPrime matches one of our known primes, and if so return
 * it's subprime value */
const SECItem *sftk_VerifyDH_Prime(SECItem *dhPrime, SECItem *generator, PRBool isFIPS);
/* check if dhSubPrime claims dhPrime is a safe prime. */
SECStatus sftk_IsSafePrime(SECItem *dhPrime, SECItem *dhSubPrime, PRBool *isSafe);
/* map an operation Attribute to a Mechanism flag */
CK_FLAGS sftk_AttributeToFlags(CK_ATTRIBUTE_TYPE op);
/* check the FIPS table to determine if this current operation is allowed by
 * FIPS security policy */
PRBool sftk_operationIsFIPS(SFTKSlot *slot, CK_MECHANISM *mech,
                            CK_ATTRIBUTE_TYPE op, SFTKObject *source,
                            CK_ULONG targetKeySize);
/* manage the fips flag on objects */
void sftk_setFIPS(SFTKObject *obj, PRBool isFIPS);
PRBool sftk_hasFIPS(SFTKObject *obj);

/* add validation objects to the slot */
CK_RV sftk_CreateValidationObjects(SFTKSlot *slot);

/* get the length of an MLDSASignature based on the PKCS #11 parameter set */
unsigned int sftk_MLDSAGetSigLen(CK_ML_DSA_PARAMETER_SET_TYPE paramSet);

SEC_END_PROTOS

#endif /* _PKCS11I_H_ */
