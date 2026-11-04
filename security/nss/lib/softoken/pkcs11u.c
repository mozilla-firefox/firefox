/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Internal PKCS #11 functions. Should only be called by pkcs11.c
 */
#include "pkcs11.h"
#include "pkcs11i.h"
#include "lowkeyi.h"
#include "secasn1.h"
#include "blapi.h"
#include "secerr.h"
#include "prnetdb.h" /* for PR_ntohl */
#include "sftkdb.h"
#include "softoken.h"
#include "secoid.h"
#include "softkver.h"

#if !defined(NSS_FIPS_DISABLED) && defined(NSS_ENABLE_FIPS_INDICATORS)
/* handle special cases. Classes require existing code to already be
 * in place for that class */
typedef enum {
    SFTKFIPSNone = 0,
    SFTKFIPSDH,           /* allow only specific primes */
    SFTKFIPSECC,          /* not just keys but specific curves */
    SFTKFIPSAEAD,         /* single shot AEAD functions not allowed in FIPS mode */
    SFTKFIPSRSAPSS,       /* make sure salt isn't too big */
    SFTKFIPSPBKDF2,       /* handle pbkdf2 FIPS restrictions */
    SFTKFIPSTlsKeyCheck,  /* check the output of TLS prf functions */
    SFTKFIPSChkHash,      /* make sure the base hash of KDF functions is FIPS */
    SFTKFIPSChkHashTls,   /* make sure the base hash of TLS KDF functions is FIPS */
    SFTKFIPSChkHashSp800, /* make sure the base hash of SP-800-108 KDF functions is FIPS */
    SFTKFIPSRSAOAEP,      /* make sure that both hashes use the same FIPS compliant algorithm */
} SFTKFIPSSpecialClass;

typedef struct SFTKFIPSAlgorithmListStr SFTKFIPSAlgorithmList;
struct SFTKFIPSAlgorithmListStr {
    CK_MECHANISM_TYPE type;
    CK_MECHANISM_INFO info;
    CK_ULONG step;
    SFTKFIPSSpecialClass special;
    size_t offset;
};
/* this file should be supplied by the vendor and include all the
 * algorithms which have Algorithm certs and have been reviewed by
 * the lab. A blank file is included for the base so that FIPS mode
 * will still be compiled and run, but FIPS indicators will always
 * return PR_FALSE
 */
#include "fips_algorithms.h"

#ifndef SFTKFIPS_PBKDF2_MIN_PW_LEN /* allow fips_algorithms.h to override */
#define SFTKFIPS_PBKDF2_MIN_PW_LEN 8
#endif
#define NSS_HAS_FIPS_INDICATORS 1
#endif

/*
 * ******************** Error mapping *******************************
 */
/*
 * map all the SEC_ERROR_xxx error codes that may be returned by freebl
 * functions to CKR_xxx.  return CKR_DEVICE_ERROR by default for backward
 * compatibility.
 */
CK_RV
sftk_MapCryptError(int error)
{
    switch (error) {
        case SEC_ERROR_INVALID_ARGS:
        case SEC_ERROR_BAD_DATA: /* MP_RANGE gets mapped to this */
            return CKR_ARGUMENTS_BAD;
        case SEC_ERROR_INPUT_LEN:
            return CKR_DATA_LEN_RANGE;
        case SEC_ERROR_OUTPUT_LEN:
            return CKR_BUFFER_TOO_SMALL;
        case SEC_ERROR_LIBRARY_FAILURE:
            return CKR_GENERAL_ERROR;
        case SEC_ERROR_NO_MEMORY:
            return CKR_HOST_MEMORY;
        case SEC_ERROR_BAD_SIGNATURE:
            return CKR_SIGNATURE_INVALID;
        case SEC_ERROR_INVALID_KEY:
            return CKR_KEY_SIZE_RANGE;
        case SEC_ERROR_BAD_KEY:        /* an EC public key that fails validation */
            return CKR_KEY_SIZE_RANGE; /* the closest error code */
        case SEC_ERROR_UNSUPPORTED_EC_POINT_FORM:
            return CKR_TEMPLATE_INCONSISTENT;
        case SEC_ERROR_UNSUPPORTED_KEYALG:
            return CKR_MECHANISM_INVALID;
        case SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE:
            return CKR_DOMAIN_PARAMS_INVALID;
        /* key pair generation failed after max number of attempts */
        case SEC_ERROR_NEED_RANDOM:
            return CKR_FUNCTION_FAILED;
    }
    return CKR_DEVICE_ERROR;
}

/*
 * functions which adjust the mapping based on different contexts
 * (Decrypt or Verify).
 */

/* used by Decrypt and UnwrapKey (indirectly) and Decrypt message */
CK_RV
sftk_MapDecryptError(int error)
{
    switch (error) {
        /* usually a padding error, or aead tag mismatch */
        case SEC_ERROR_BAD_DATA:
            return CKR_ENCRYPTED_DATA_INVALID;
        default:
            return sftk_MapCryptError(error);
    }
}

/*
 * return CKR_SIGNATURE_INVALID instead of CKR_DEVICE_ERROR by default for
 * backward compatibilty.
 */
CK_RV
sftk_MapVerifyError(int error)
{
    CK_RV crv = sftk_MapCryptError(error);
    if (crv == CKR_DEVICE_ERROR)
        crv = CKR_SIGNATURE_INVALID;
    return crv;
}

/*
 * ******************** Attribute Utilities *******************************
 */

/*
 * create a new attribute with type, value, and length. Space is allocated
 * to hold value.
 */
static SFTKAttribute *
sftk_InitAttribute(SFTKSessionObject *so, int index,
                   CK_ATTRIBUTE_TYPE type, const void *value, CK_ULONG len)
{
    SFTKAttribute *attribute;

    PORT_Assert(index < MAX_OBJS_ATTRS);
    if (index >= MAX_OBJS_ATTRS)
        return NULL;

    attribute = &so->attrList[index];
    attribute->attrib.type = type;
    attribute->freeAttr = PR_FALSE;
    attribute->freeData = PR_FALSE;
    if (value) {
        if (len <= ATTR_SPACE) {
            attribute->attrib.pValue = attribute->space;
        } else {
            attribute->attrib.pValue = PORT_Alloc(len);
            attribute->freeData = PR_TRUE;
        }
        if (attribute->attrib.pValue == NULL) {
            return NULL;
        }
        PORT_Memcpy(attribute->attrib.pValue, value, len);
        attribute->attrib.ulValueLen = len;
    } else {
        attribute->attrib.pValue = NULL;
        attribute->attrib.ulValueLen = 0;
    }
    attribute->attrib.type = type;
    attribute->handle = type;
    attribute->next = attribute->prev = NULL;
    return attribute;
}

static SFTKAttribute *
sftk_NewAttribute(SFTKObject *object,
                  CK_ATTRIBUTE_TYPE type, const void *value, CK_ULONG len)
{
    SFTKSessionObject *so = sftk_narrowToSessionObject(object);
    int index;

    if (so == NULL) {
        /* allocate new attribute in a buffer */
        PORT_Assert(0);
        return NULL;
    }
    /*
     * We attempt to keep down contention on Malloc and Arena locks by
     * limiting the number of these calls on high traversed paths. This
     * is done for attributes by 'allocating' them from a pool already
     * allocated by the parent object.
     */
    PR_Lock(so->attributeLock);
    index = so->nextAttr++;
    PR_Unlock(so->attributeLock);
    return sftk_InitAttribute(so, index, type, value, len);
}

/*
 * Return whether a non-NULL attribute value must be passed to PORT_Free,
 * judged by where pValue points; abort if freeData disagrees. Freeing an
 * inline value would hand the allocator a pointer into a live structure,
 * which it may treat as freeing the whole structure. If so is not NULL, a
 * heap value must also lie outside its attrList.
 */
static PRBool
sftk_attributeOwnsHeapValue(const SFTKSessionObject *so,
                            const SFTKAttribute *attribute)
{
    uintptr_t value = (uintptr_t)attribute->attrib.pValue;
    PRBool freeData = attribute->freeData;

    if (value == (uintptr_t)attribute->space) {
        PORT_ReleaseAssert(freeData == PR_FALSE &&
                           attribute->attrib.ulValueLen <= ATTR_SPACE);
        return PR_FALSE;
    }
    PORT_ReleaseAssert(freeData == PR_TRUE);
    if (so) {
        uintptr_t start = (uintptr_t)so->attrList;
        PORT_ReleaseAssert(value < start ||
                           value >= start + sizeof(so->attrList));
    }
    return PR_TRUE;
}

/*
 * Free up all the memory associated with an attribute. Reference count
 * must be zero to call this.
 */
static void
sftk_DestroyAttribute(SFTKAttribute *attribute)
{
    if (attribute->attrib.pValue) {
        PRBool ownsHeapValue = sftk_attributeOwnsHeapValue(NULL, attribute);
        /* clear out the data in the attribute value... it may have been
         * sensitive data */
        PORT_Memset(attribute->attrib.pValue, 0, attribute->attrib.ulValueLen);
        if (ownsHeapValue) {
            PORT_Free(attribute->attrib.pValue);
            attribute->attrib.pValue = NULL;
            attribute->freeData = PR_FALSE;
        }
    }
    if (attribute->freeAttr) {
        PORT_Free(attribute);
    }
}

/*
 * release a reference to an attribute structure
 */
void
sftk_FreeAttribute(SFTKAttribute *attribute)
{
    if (attribute && attribute->freeAttr) {
        sftk_DestroyAttribute(attribute);
        return;
    }
}

static SFTKAttribute *
sftk_FindTokenAttribute(SFTKTokenObject *object, CK_ATTRIBUTE_TYPE type)
{
    SFTKAttribute *myattribute = NULL;
    SFTKDBHandle *dbHandle = NULL;
    CK_RV crv = CKR_HOST_MEMORY;

    if (object == NULL) {
        return NULL;
    }
    myattribute = (SFTKAttribute *)PORT_Alloc(sizeof(SFTKAttribute));
    if (myattribute == NULL) {
        goto loser;
    }

    dbHandle = sftk_getDBForTokenObject(object->obj.slot, object->obj.handle);

    myattribute->handle = type;
    myattribute->attrib.type = type;
    myattribute->attrib.pValue = myattribute->space;
    myattribute->attrib.ulValueLen = ATTR_SPACE;
    myattribute->next = myattribute->prev = NULL;
    myattribute->freeAttr = PR_TRUE;
    myattribute->freeData = PR_FALSE;

    crv = sftkdb_GetAttributeValue(dbHandle, object->obj.handle,
                                   &myattribute->attrib, 1);

    /* attribute is bigger than our attribute space buffer, malloc it */
    if (crv == CKR_BUFFER_TOO_SMALL) {
        myattribute->attrib.pValue = NULL;
        crv = sftkdb_GetAttributeValue(dbHandle, object->obj.handle,
                                       &myattribute->attrib, 1);
        if (crv != CKR_OK) {
            goto loser;
        }
        myattribute->attrib.pValue = PORT_Alloc(myattribute->attrib.ulValueLen);
        if (myattribute->attrib.pValue == NULL) {
            crv = CKR_HOST_MEMORY;
            goto loser;
        }
        myattribute->freeData = PR_TRUE;
        crv = sftkdb_GetAttributeValue(dbHandle, object->obj.handle,
                                       &myattribute->attrib, 1);
    }
loser:
    if (dbHandle) {
        sftk_freeDB(dbHandle);
    }
    if (crv != CKR_OK) {
        if (myattribute) {
            myattribute->attrib.ulValueLen = 0;
            sftk_FreeAttribute(myattribute);
            myattribute = NULL;
        }
    }
    return myattribute;
}

/*
 * Make a detached, owned copy of a session object's attribute. Must be
 * called with sessObject->attributeLock held so the (pValue, ulValueLen)
 * pair is captured atomically with respect to sftk_forceAttribute() and
 * friends. The copy carries freeAttr = PR_TRUE, so sftk_FreeAttribute()
 * releases it; callers may read it without holding any lock.
 */
static SFTKAttribute *
sftk_CopyAttribute(const SFTKAttribute *attribute)
{
    SFTKAttribute *copy = (SFTKAttribute *)PORT_Alloc(sizeof(SFTKAttribute));
    if (copy == NULL) {
        return NULL;
    }
    copy->next = copy->prev = NULL;
    copy->freeAttr = PR_TRUE;
    copy->freeData = PR_FALSE;
    copy->handle = attribute->handle;
    copy->attrib.type = attribute->attrib.type;
    if (attribute->attrib.pValue == NULL) {
        copy->attrib.pValue = NULL;
        copy->attrib.ulValueLen = 0;
        return copy;
    }
    if (attribute->attrib.ulValueLen <= ATTR_SPACE) {
        copy->attrib.pValue = copy->space;
    } else {
        copy->attrib.pValue = PORT_Alloc(attribute->attrib.ulValueLen);
        if (copy->attrib.pValue == NULL) {
            PORT_Free(copy);
            return NULL;
        }
        copy->freeData = PR_TRUE;
    }
    PORT_Memcpy(copy->attrib.pValue, attribute->attrib.pValue,
                attribute->attrib.ulValueLen);
    copy->attrib.ulValueLen = attribute->attrib.ulValueLen;
    return copy;
}

/*
 * look up and attribute structure from a type and Object structure.
 * The returned attribute is referenced and needs to be freed when
 * it is no longer needed.
 */
SFTKAttribute *
sftk_FindAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type)
{
    SFTKAttribute *attribute;
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);

    /* validation flags are stored as FIPS indicators */
    if (type == CKA_OBJECT_VALIDATION_FLAGS) {
        return &object->validation_attribute;
    }

    if (sessObject == NULL) {
        return sftk_FindTokenAttribute(sftk_narrowToTokenObject(object), type);
    }

    /* Return a detached copy rather than the live pool entry: callers
     * dereference attrib.pValue after the lock is dropped, while a
     * concurrent sftk_forceAttribute() may free and reassign it.
     * Snapshotting under the lock gives the caller a stable,
     * private value (this matches the token-object path above). */
    PR_Lock(sessObject->attributeLock);
    sftkqueue_find(attribute, type, sessObject->head, sessObject->hashSize);
    if (attribute != NULL) {
        attribute = sftk_CopyAttribute(attribute);
    }
    PR_Unlock(sessObject->attributeLock);

    return (attribute);
}

/*
 * Look up an attribute while the caller holds the object's attributeLock.
 * Returns a BORROWED reference to the live node in sessObject->head[] (or
 * the object's embedded validation attribute): valid only until the lock is
 * released, and never to be passed to sftk_FreeAttribute().
 *
 * This is only valid for session objects (and CKA_OBJECT_VALIDATION_FLAGS).
 * Token objects have no live node; callers that may see one must fall back
 * to sftk_FindAttribute(). Returns NULL if the attribute is absent.
 *
 * The lock is NOT reentrant: while holding it, the caller must not call any
 * attribute API that takes it itself (sftk_FindAttribute,
 * sftk_AddAttributeType, sftk_forceAttribute, sftk_DeleteAttributeType).
 */
static SFTKAttribute *
sftk_FindAttributeLocked(SFTKObject *object, CK_ATTRIBUTE_TYPE type)
{
    SFTKSessionObject *sessObject;
    SFTKAttribute *attribute;

    /* validation flags are stored as FIPS indicators */
    if (type == CKA_OBJECT_VALIDATION_FLAGS) {
        return &object->validation_attribute;
    }

    sessObject = sftk_narrowToSessionObject(object);
    PORT_Assert(sessObject != NULL);
    if (sessObject == NULL) {
        return NULL;
    }

    sftkqueue_find(attribute, type, sessObject->head, sessObject->hashSize);
    return attribute;
}

/*
 * Take a buffer and it's length and return it's true size in bits;
 */
unsigned int
sftk_GetLengthInBits(unsigned char *buf, unsigned int bufLen)
{
    unsigned int size = bufLen * 8;
    unsigned int i;

    /* Get the real length in bytes */
    for (i = 0; i < bufLen; i++) {
        unsigned char c = *buf++;
        if (c != 0) {
            unsigned char m;
            for (m = 0x80; m > 0; m = m >> 1) {
                if ((c & m) != 0) {
                    break;
                }
                size--;
            }
            break;
        }
        size -= 8;
    }
    return size;
}

/*
 * Constrain a big num attribute. to size and padding
 * minLength means length of the object must be greater than equal to minLength
 * maxLength means length of the object must be less than equal to maxLength
 * minMultiple means that object length mod minMultiple must equal 0.
 * all input sizes are in bits.
 * if any constraint is '0' that constraint is not checked.
 */
CK_RV
sftk_ConstrainAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                        int minLength, int maxLength, int minMultiple)
{
    SFTKAttribute *attribute;
    int size = 0;
    unsigned char *ptr;
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    CK_RV crv = CKR_OK;

    if (sessObject != NULL) {
        PR_Lock(sessObject->attributeLock);
        attribute = sftk_FindAttributeLocked(object, type);
    } else {
        attribute = sftk_FindAttribute(object, type);
    }

    if (!attribute) {
        crv = CKR_TEMPLATE_INCOMPLETE;
    } else {
        ptr = (unsigned char *)attribute->attrib.pValue;
        if (ptr == NULL) {
            crv = CKR_ATTRIBUTE_VALUE_INVALID;
        } else {
            size = sftk_GetLengthInBits(ptr, attribute->attrib.ulValueLen);
        }
    }

    if (sessObject != NULL) {
        PR_Unlock(sessObject->attributeLock);
    } else {
        sftk_FreeAttribute(attribute);
    }
    if (crv != CKR_OK) {
        return crv;
    }

    if ((minLength != 0) && (size < minLength)) {
        return CKR_ATTRIBUTE_VALUE_INVALID;
    }
    if ((maxLength != 0) && (size > maxLength)) {
        return CKR_ATTRIBUTE_VALUE_INVALID;
    }
    if ((minMultiple != 0) && ((size % minMultiple) != 0)) {
        return CKR_ATTRIBUTE_VALUE_INVALID;
    }
    return CKR_OK;
}

PRBool
sftk_hasAttributeToken(SFTKTokenObject *object, CK_ATTRIBUTE_TYPE type)
{
    CK_ATTRIBUTE template;
    CK_RV crv;
    SFTKDBHandle *dbHandle;

    if (object == NULL) {
        return PR_FALSE;
    }
    dbHandle = sftk_getDBForTokenObject(object->obj.slot, object->obj.handle);
    template.type = type;
    template.pValue = NULL;
    template.ulValueLen = 0;

    crv = sftkdb_GetAttributeValue(dbHandle, object->obj.handle, &template, 1);
    sftk_freeDB(dbHandle);

    /* attribute is bigger than our attribute space buffer, malloc it */
    return (crv == CKR_OK) ? PR_TRUE : PR_FALSE;
}

/*
 * return true if object has attribute
 */
PRBool
sftk_hasAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type)
{
    SFTKAttribute *attribute;
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);

    if (sessObject == NULL) {
        return sftk_hasAttributeToken(sftk_narrowToTokenObject(object), type);
    }

    PR_Lock(sessObject->attributeLock);
    sftkqueue_find(attribute, type, sessObject->head, sessObject->hashSize);
    PR_Unlock(sessObject->attributeLock);

    return (PRBool)(attribute != NULL);
}

/*
 * add an attribute to an object
 */
static void
sftk_AddAttribute(SFTKObject *object, SFTKAttribute *attribute)
{
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);

    if (sessObject == NULL)
        return;
    PR_Lock(sessObject->attributeLock);
    sftkqueue_add(attribute, attribute->handle,
                  sessObject->head, sessObject->hashSize);
    PR_Unlock(sessObject->attributeLock);
}

/*
 * copy an unsigned attribute into a SECItem. Secitem is allocated in
 * the specified arena.
 */
CK_RV
sftk_Attribute2SSecItem(PLArenaPool *arena, SECItem *item, SFTKObject *object,
                        CK_ATTRIBUTE_TYPE type)
{
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    SFTKAttribute *attribute;
    CK_RV crv = CKR_OK;

    item->data = NULL;

    if (sessObject != NULL) {
        PR_Lock(sessObject->attributeLock);
        attribute = sftk_FindAttributeLocked(object, type);
    } else {
        attribute = sftk_FindAttribute(object, type);
    }

    if (attribute == NULL) {
        crv = CKR_TEMPLATE_INCOMPLETE;
    } else {
        (void)SECITEM_AllocItem(arena, item, attribute->attrib.ulValueLen);
        if (item->data == NULL) {
            crv = CKR_HOST_MEMORY;
        } else {
            PORT_Memcpy(item->data, attribute->attrib.pValue, item->len);
        }
    }

    if (sessObject != NULL) {
        PR_Unlock(sessObject->attributeLock);
    } else {
        sftk_FreeAttribute(attribute);
    }
    return crv;
}

/*
 * fetch multiple attributes into  SECItems. Secitem data is allocated in
 * the specified arena.
 */
CK_RV
sftk_MultipleAttribute2SecItem(PLArenaPool *arena, SFTKObject *object,
                               SFTKItemTemplate *itemTemplate, int itemTemplateCount)
{

    CK_RV crv = CKR_OK;
    CK_ATTRIBUTE templateSpace[SFTK_MAX_ITEM_TEMPLATE];
    CK_ATTRIBUTE *template;
    SFTKTokenObject *tokObject;
    SFTKDBHandle *dbHandle = NULL;
    int i;

    tokObject = sftk_narrowToTokenObject(object);

    /* session objects, just loop through the list */
    if (tokObject == NULL) {
        for (i = 0; i < itemTemplateCount; i++) {
            crv = sftk_Attribute2SecItem(arena, itemTemplate[i].item, object,
                                         itemTemplate[i].type);
            if (crv != CKR_OK) {
                return crv;
            }
        }
        return CKR_OK;
    }

    /* don't do any work if none is required */
    if (itemTemplateCount == 0) {
        return CKR_OK;
    }

    /* don't allocate the template unless we need it */
    if (itemTemplateCount > SFTK_MAX_ITEM_TEMPLATE) {
        template = PORT_NewArray(CK_ATTRIBUTE, itemTemplateCount);
    } else {
        template = templateSpace;
    }

    if (template == NULL) {
        crv = CKR_HOST_MEMORY;
        goto loser;
    }

    dbHandle = sftk_getDBForTokenObject(object->slot, object->handle);
    if (dbHandle == NULL) {
        crv = CKR_OBJECT_HANDLE_INVALID;
        goto loser;
    }

    /* set up the PKCS #11 template */
    for (i = 0; i < itemTemplateCount; i++) {
        template[i].type = itemTemplate[i].type;
        template[i].pValue = NULL;
        template[i].ulValueLen = 0;
    }

    /* fetch the attribute lengths */
    crv = sftkdb_GetAttributeValue(dbHandle, object->handle,
                                   template, itemTemplateCount);
    if (crv != CKR_OK) {
        goto loser;
    }

    /* allocate space for the attributes */
    for (i = 0; i < itemTemplateCount; i++) {
        template[i].pValue = PORT_ArenaAlloc(arena, template[i].ulValueLen);
        if (template[i].pValue == NULL) {
            crv = CKR_HOST_MEMORY;
            goto loser;
        }
    }

    /* fetch the attributes */
    crv = sftkdb_GetAttributeValue(dbHandle, object->handle,
                                   template, itemTemplateCount);
    if (crv != CKR_OK) {
        goto loser;
    }

    /* Fill in the items */
    for (i = 0; i < itemTemplateCount; i++) {
        itemTemplate[i].item->data = template[i].pValue;
        itemTemplate[i].item->len = template[i].ulValueLen;
    }

loser:
    if (template != templateSpace) {
        PORT_Free(template);
    }
    if (dbHandle) {
        sftk_freeDB(dbHandle);
    }

    return crv;
}

/*
 * this is only valid for CK_BBOOL type attributes. Return the state
 * of that attribute.
 */
PRBool
sftk_isTrue(SFTKObject *object, CK_ATTRIBUTE_TYPE type)
{
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    SFTKAttribute *attribute;
    PRBool tok = PR_FALSE;

    /* For a session object read the live single-byte value under the lock
     * rather than copying it out (this is a very hot path); for a token
     * object fall back to a freed copy. */
    if (sessObject != NULL) {
        PR_Lock(sessObject->attributeLock);
        attribute = sftk_FindAttributeLocked(object, type);
    } else {
        attribute = sftk_FindAttribute(object, type);
    }

    if (attribute != NULL && attribute->attrib.pValue != NULL) {
        tok = (PRBool)(*(CK_BBOOL *)attribute->attrib.pValue);
    }

    if (sessObject != NULL) {
        PR_Unlock(sessObject->attributeLock);
    } else {
        sftk_FreeAttribute(attribute);
    }

    return tok;
}

static CK_RV
sftk_forceTokenAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                         const void *value, unsigned int len)
{
    CK_ATTRIBUTE attribute;
    SFTKDBHandle *dbHandle = NULL;
    SFTKTokenObject *to = sftk_narrowToTokenObject(object);
    CK_RV crv;

    PORT_Assert(to);
    if (to == NULL) {
        return CKR_DEVICE_ERROR;
    }

    dbHandle = sftk_getDBForTokenObject(object->slot, object->handle);

    attribute.type = type;
    attribute.pValue = (void *)value;
    attribute.ulValueLen = len;

    crv = sftkdb_SetAttributeValue(dbHandle, object, &attribute, 1);
    sftk_freeDB(dbHandle);
    return crv;
}

/*
 * force an attribute to a specifc value.
 */
CK_RV
sftk_forceAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                    const void *value, unsigned int len)
{
    SFTKAttribute *attribute;
    void *att_val = NULL;
    PRBool freeData = PR_FALSE;
    PRBool oldOwnsHeapValue;

    PORT_Assert(object);
    PORT_Assert(object->refCount);
    PORT_Assert(object->slot);
    if (!object ||
        !object->refCount ||
        !object->slot) {
        return CKR_DEVICE_ERROR;
    }
    /* validation flags are stored as FIPS indicators,
     * don't send them through the general attribute
     * parsing */
    if (type == CKA_OBJECT_VALIDATION_FLAGS) {
        CK_FLAGS validation;
        if (len != sizeof(CK_FLAGS)) {
            return CKR_ATTRIBUTE_VALUE_INVALID;
        }
        validation = *(CK_FLAGS *)value;
        /* we only understand FIPS currently, don't allow setting
         * other flags */
        if ((validation & ~SFTK_VALIDATION_FIPS_FLAG) != 0) {
            return CKR_ATTRIBUTE_VALUE_INVALID;
        }
        object->validation_value = validation;
        return CKR_OK;
    }
    if (sftk_isToken(object->handle)) {
        return sftk_forceTokenAttribute(object, type, value, len);
    }
    /* After the token/validation dispatch above, this is a session
     * object. Look up and mutate the live node under attributeLock so the
     * (pValue, ulValueLen) pair stays consistent for concurrent readers,
     * which snapshot it under the same lock in sftk_FindAttribute(). */
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    PORT_Assert(sessObject);
    if (sessObject == NULL) {
        return CKR_DEVICE_ERROR;
    }

    PR_Lock(sessObject->attributeLock);
    sftkqueue_find(attribute, type, sessObject->head, sessObject->hashSize);
    if (attribute == NULL) {
        PR_Unlock(sessObject->attributeLock);
        return sftk_AddAttributeType(object, type, value, len);
    }
    oldOwnsHeapValue = attribute->attrib.pValue != NULL &&
                       sftk_attributeOwnsHeapValue(sessObject, attribute);

    if (value) {
        if (len <= ATTR_SPACE) {
            att_val = attribute->space;
        } else {
            att_val = PORT_Alloc(len);
            freeData = PR_TRUE;
        }
        if (att_val == NULL) {
            PR_Unlock(sessObject->attributeLock);
            return CKR_HOST_MEMORY;
        }
        if (attribute->attrib.pValue == att_val) {
            PORT_Memset(attribute->attrib.pValue, 0,
                        attribute->attrib.ulValueLen);
        }
        PORT_Memcpy(att_val, value, len);
    }
    if (attribute->attrib.pValue != NULL) {
        if (attribute->attrib.pValue != att_val) {
            PORT_Memset(attribute->attrib.pValue, 0,
                        attribute->attrib.ulValueLen);
        }
        if (oldOwnsHeapValue) {
            PORT_ReleaseAssert(attribute->attrib.pValue != att_val);
            PORT_Free(attribute->attrib.pValue);
        }
        attribute->freeData = PR_FALSE;
        attribute->attrib.pValue = NULL;
        attribute->attrib.ulValueLen = 0;
    }
    if (att_val) {
        attribute->attrib.pValue = att_val;
        attribute->attrib.ulValueLen = len;
        attribute->freeData = freeData;
    }
    PR_Unlock(sessObject->attributeLock);
    return CKR_OK;
}

/*
 * decode when a particular attribute may be modified
 *  SFTK_NEVER: This attribute must be set at object creation time and
 *  can never be modified.
 *  SFTK_ONCOPY: This attribute may be modified only when you copy the
 *  object.
 *  SFTK_SENSITIVE: The CKA_SENSITIVE attribute can only be changed from
 *  CK_FALSE to CK_TRUE.
 *  SFTK_ALWAYS: This attribute can always be modified.
 * Some attributes vary their modification type based on the class of the
 *   object.
 */
SFTKModifyType
sftk_modifyType(CK_ATTRIBUTE_TYPE type, CK_OBJECT_CLASS inClass)
{
    /* if we don't know about it, user user defined, always allow modify */
    SFTKModifyType mtype = SFTK_ALWAYS;

    switch (type) {
        /* NEVER */
        case CKA_CLASS:
        case CKA_CERTIFICATE_TYPE:
        case CKA_KEY_TYPE:
        case CKA_MODULUS:
        case CKA_MODULUS_BITS:
        case CKA_PUBLIC_EXPONENT:
        case CKA_PRIVATE_EXPONENT:
        case CKA_PRIME:
        case CKA_BASE:
        case CKA_PRIME_1:
        case CKA_PRIME_2:
        case CKA_EXPONENT_1:
        case CKA_EXPONENT_2:
        case CKA_COEFFICIENT:
        case CKA_SEED:
        case CKA_PARAMETER_SET:
        case CKA_VALUE_LEN:
        case CKA_ALWAYS_SENSITIVE:
        case CKA_NEVER_EXTRACTABLE:
        case CKA_NSS_DB:
            mtype = SFTK_NEVER;
            break;

        /* ONCOPY */
        case CKA_TOKEN:
        case CKA_PRIVATE:
        case CKA_MODIFIABLE:
            mtype = SFTK_ONCOPY;
            break;

        /* SENSITIVE */
        case CKA_SENSITIVE:
        case CKA_EXTRACTABLE:
            mtype = SFTK_SENSITIVE;
            break;

        /* ALWAYS */
        case CKA_LABEL:
        case CKA_APPLICATION:
        case CKA_ID:
        case CKA_SERIAL_NUMBER:
        case CKA_START_DATE:
        case CKA_END_DATE:
        case CKA_DERIVE:
        case CKA_ENCRYPT:
        case CKA_DECRYPT:
        case CKA_SIGN:
        case CKA_VERIFY:
        case CKA_SIGN_RECOVER:
        case CKA_VERIFY_RECOVER:
        case CKA_WRAP:
        case CKA_UNWRAP:
            mtype = SFTK_ALWAYS;
            break;

        /* DEPENDS ON CLASS */
        case CKA_VALUE:
            mtype = (inClass == CKO_DATA) ? SFTK_ALWAYS : SFTK_NEVER;
            break;

        case CKA_SUBPRIME:
            /* allow the CKA_SUBPRIME to be added to dh private keys */
            mtype = (inClass == CKO_PRIVATE_KEY) ? SFTK_ALWAYS : SFTK_NEVER;
            break;

        case CKA_SUBJECT:
            mtype = (inClass == CKO_CERTIFICATE) ? SFTK_NEVER : SFTK_ALWAYS;
            break;
        default:
            break;
    }
    return mtype;
}

/* decode if a particular attribute is sensitive (cannot be read
 * back to the user of if the object is set to SENSITIVE) */
PRBool
sftk_isSensitive(CK_ATTRIBUTE_TYPE type, CK_OBJECT_CLASS inClass)
{
    switch (type) {
        /* ALWAYS */
        case CKA_PRIVATE_EXPONENT:
        case CKA_PRIME_1:
        case CKA_PRIME_2:
        case CKA_EXPONENT_1:
        case CKA_EXPONENT_2:
        case CKA_COEFFICIENT:
        case CKA_SEED:
            return PR_TRUE;

        /* DEPENDS ON CLASS */
        case CKA_VALUE:
            /* PRIVATE and SECRET KEYS have SENSITIVE values */
            return (PRBool)((inClass == CKO_PRIVATE_KEY) || (inClass == CKO_SECRET_KEY));

        default:
            break;
    }
    return PR_FALSE;
}

/*
 * copy an attribute into a SECItem. Secitem is allocated in the specified
 * arena.
 */
CK_RV
sftk_Attribute2SecItem(PLArenaPool *arena, SECItem *item, SFTKObject *object,
                       CK_ATTRIBUTE_TYPE type)
{
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    SFTKAttribute *attribute;
    CK_RV crv = CKR_OK;
    int len;

    if (sessObject != NULL) {
        PR_Lock(sessObject->attributeLock);
        attribute = sftk_FindAttributeLocked(object, type);
    } else {
        attribute = sftk_FindAttribute(object, type);
    }

    if (attribute == NULL) {
        crv = CKR_TEMPLATE_INCOMPLETE;
    } else {
        len = attribute->attrib.ulValueLen;
        if (arena) {
            item->data = (unsigned char *)PORT_ArenaAlloc(arena, len);
        } else {
            item->data = (unsigned char *)PORT_Alloc(len);
        }
        if (item->data == NULL) {
            crv = CKR_HOST_MEMORY;
        } else {
            item->len = len;
            PORT_Memcpy(item->data, attribute->attrib.pValue, len);
        }
    }

    if (sessObject != NULL) {
        PR_Unlock(sessObject->attributeLock);
    } else {
        sftk_FreeAttribute(attribute);
    }
    return crv;
}

CK_RV
sftk_GetULongAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                       CK_ULONG *longData)
{
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    SFTKAttribute *attribute;
    CK_RV crv = CKR_OK;

    /* For a session object read the live value under the lock instead of
     * copying it out; for a token object fall back to a freed copy. */
    if (sessObject != NULL) {
        PR_Lock(sessObject->attributeLock);
        attribute = sftk_FindAttributeLocked(object, type);
    } else {
        attribute = sftk_FindAttribute(object, type);
    }

    if (attribute == NULL) {
        crv = CKR_TEMPLATE_INCOMPLETE;
    } else if (attribute->attrib.ulValueLen != sizeof(CK_ULONG)) {
        crv = CKR_ATTRIBUTE_VALUE_INVALID;
    } else {
        *longData = *(CK_ULONG *)attribute->attrib.pValue;
    }

    if (sessObject != NULL) {
        PR_Unlock(sessObject->attributeLock);
    } else {
        sftk_FreeAttribute(attribute);
    }
    return crv;
}

CK_RV
sftk_ReadAttribute(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                   unsigned char *data, unsigned int maxLen, unsigned int *lenp)
{
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);
    SFTKAttribute *attribute;
    CK_RV crv = CKR_OK;

    /* For a session object read the live value under the lock instead of
     * copying it out; for a token object fall back to a freed copy. */
    if (sessObject != NULL) {
        PR_Lock(sessObject->attributeLock);
        attribute = sftk_FindAttributeLocked(object, type);
    } else {
        attribute = sftk_FindAttribute(object, type);
    }

    if (attribute == NULL) {
        crv = CKR_TEMPLATE_INCOMPLETE;
    } else {
        *lenp = attribute->attrib.ulValueLen;
        if (*lenp > maxLen) {
            /* normally would be CKR_BUFFER_TOO_SMALL, but
             * it used with internal buffers, so if the value is
             * to long, the original attribute was invalid */
            crv = CKR_ATTRIBUTE_VALUE_INVALID;
        } else {
            PORT_Memcpy(data, attribute->attrib.pValue, *lenp);
        }
    }

    if (sessObject != NULL) {
        PR_Unlock(sessObject->attributeLock);
    } else {
        sftk_FreeAttribute(attribute);
    }
    return crv;
}

void
sftk_DeleteAttributeType(SFTKObject *object, CK_ATTRIBUTE_TYPE type)
{
    SFTKAttribute *attribute;
    SFTKSessionObject *sessObject = sftk_narrowToSessionObject(object);

    if (sessObject == NULL) {
        return;
    }

    /* Find, unlink, and destroy the live node atomically under the lock.
     * Once unlinked from head[] the node is unreachable by any other
     * thread (readers snapshot copies under the same lock), so destroying
     * it here cannot race a concurrent reader or a second deleter. */
    PR_Lock(sessObject->attributeLock);
    sftkqueue_find(attribute, type, sessObject->head, sessObject->hashSize);
    if (attribute != NULL) {
        if (sftkqueue_is_queued(attribute, attribute->handle,
                                sessObject->head, sessObject->hashSize)) {
            sftkqueue_delete(attribute, attribute->handle,
                             sessObject->head, sessObject->hashSize);
        }
        sftk_DestroyAttribute(attribute);
    }
    PR_Unlock(sessObject->attributeLock);
}

CK_RV
sftk_AddAttributeType(SFTKObject *object, CK_ATTRIBUTE_TYPE type,
                      const void *valPtr, CK_ULONG length)
{
    SFTKAttribute *attribute;
    attribute = sftk_NewAttribute(object, type, valPtr, length);
    if (attribute == NULL) {
        return CKR_HOST_MEMORY;
    }
    sftk_AddAttribute(object, attribute);
    return CKR_OK;
}

/*
 * ******************** Object Utilities *******************************
 */

/* must be called holding sftk_tokenKeyLock(slot) */
static SECItem *
sftk_lookupTokenKeyByHandle(SFTKSlot *slot, CK_OBJECT_HANDLE handle)
{
    return (SECItem *)PL_HashTableLookup(slot->tokObjHashTable, (void *)(uintptr_t)handle);
}

/*
 * use the refLock. This operations should be very rare, so the added
 * contention on the ref lock should be lower than the overhead of adding
 * a new lock. We use separate functions for this just in case I'm wrong.
 */
static void
sftk_tokenKeyLock(SFTKSlot *slot)
{
    SKIP_AFTER_FORK(PR_Lock(slot->objectLock));
}

static void
sftk_tokenKeyUnlock(SFTKSlot *slot)
{
    SKIP_AFTER_FORK(PR_Unlock(slot->objectLock));
}

static PRIntn
sftk_freeHashItem(PLHashEntry *entry, PRIntn index, void *arg)
{
    SECItem *item = (SECItem *)entry->value;

    SECITEM_FreeItem(item, PR_TRUE);
    return HT_ENUMERATE_NEXT;
}

CK_RV
SFTK_ClearTokenKeyHashTable(SFTKSlot *slot)
{
    sftk_tokenKeyLock(slot);
    PORT_Assert(!slot->present);
    PL_HashTableEnumerateEntries(slot->tokObjHashTable, sftk_freeHashItem, NULL);
    sftk_tokenKeyUnlock(slot);
    return CKR_OK;
}

/* allocation hooks that allow us to recycle old object structures */
static SFTKObjectFreeList sessionObjectList = { NULL, NULL, 0 };
static SFTKObjectFreeList tokenObjectList = { NULL, NULL, 0 };

SFTKObject *
sftk_GetObjectFromList(PRBool *hasLocks, PRBool optimizeSpace,
                       SFTKObjectFreeList *list, unsigned int hashSize, PRBool isSessionObject)
{
    SFTKObject *object;
    int size = 0;

    if (!optimizeSpace) {
        PR_Lock(list->lock);
        object = list->head;
        if (object) {
            list->head = object->next;
            list->count--;
        }
        PR_Unlock(list->lock);
        if (object) {
            /* A live object on the free list means it was destroyed
             * twice; some other thread still holds a pointer to it and
             * will corrupt it if it is handed out again. */
            PORT_ReleaseAssert(object->refCount == 0);
            object->next = object->prev = NULL;
            *hasLocks = PR_TRUE;
            return object;
        }
    }
    size = isSessionObject ? sizeof(SFTKSessionObject) + hashSize * sizeof(SFTKAttribute *) : sizeof(SFTKTokenObject);

    object = (SFTKObject *)PORT_ZAlloc(size);
    if (isSessionObject && object) {
        ((SFTKSessionObject *)object)->hashSize = hashSize;
    }
    *hasLocks = PR_FALSE;
    return object;
}

static void
sftk_PutObjectToList(SFTKObject *object, SFTKObjectFreeList *list,
                     PRBool isSessionObject)
{

    /* the code below is equivalent to :
     *     optimizeSpace = isSessionObject ? object->optimizeSpace : PR_FALSE;
     * just faster.
     */
    PRBool optimizeSpace = isSessionObject &&
                           ((SFTKSessionObject *)object)->optimizeSpace;
    if (object->refLock && !optimizeSpace) {
        PR_Lock(list->lock);
        if (list->count < MAX_OBJECT_LIST_SIZE) {
            object->next = list->head;
            list->head = object;
            list->count++;
            PR_Unlock(list->lock);
            return;
        }
        PR_Unlock(list->lock);
    }
    if (isSessionObject) {
        SFTKSessionObject *so = (SFTKSessionObject *)object;
        PR_DestroyLock(so->attributeLock);
        so->attributeLock = NULL;
    }
    if (object->refLock) {
        PR_DestroyLock(object->refLock);
        object->refLock = NULL;
    }
    PORT_Free(object);
}

static SFTKObject *
sftk_freeObjectData(SFTKObject *object)
{
    SFTKObject *next = object->next;

    PORT_Free(object);
    return next;
}

static void
sftk_InitFreeList(SFTKObjectFreeList *list)
{
    if (!list->lock) {
        list->lock = PR_NewLock();
    }
}

void
sftk_InitFreeLists(void)
{
    sftk_InitFreeList(&sessionObjectList);
    sftk_InitFreeList(&tokenObjectList);
}

static void
sftk_CleanupFreeList(SFTKObjectFreeList *list, PRBool isSessionList)
{
    SFTKObject *object;

    if (!list->lock) {
        return;
    }
    SKIP_AFTER_FORK(PR_Lock(list->lock));
    for (object = list->head; object != NULL;
         object = sftk_freeObjectData(object)) {
        PR_DestroyLock(object->refLock);
        if (isSessionList) {
            PR_DestroyLock(((SFTKSessionObject *)object)->attributeLock);
        }
    }
    list->count = 0;
    list->head = NULL;
    SKIP_AFTER_FORK(PR_Unlock(list->lock));
    SKIP_AFTER_FORK(PR_DestroyLock(list->lock));
    list->lock = NULL;
}

void
sftk_CleanupFreeLists(void)
{
    sftk_CleanupFreeList(&sessionObjectList, PR_TRUE);
    sftk_CleanupFreeList(&tokenObjectList, PR_FALSE);
}

/*
 * Create a new object
 */
SFTKObject *
sftk_NewObject(SFTKSlot *slot)
{
    SFTKObject *object;
    SFTKSessionObject *sessObject;
    PRBool hasLocks = PR_FALSE;
    unsigned int i;
    unsigned int hashSize = 0;

    hashSize = (slot->optimizeSpace) ? SPACE_ATTRIBUTE_HASH_SIZE : TIME_ATTRIBUTE_HASH_SIZE;

    object = sftk_GetObjectFromList(&hasLocks, slot->optimizeSpace,
                                    &sessionObjectList, hashSize, PR_TRUE);
    if (object == NULL) {
        return NULL;
    }
    sessObject = (SFTKSessionObject *)object;
    sessObject->nextAttr = 0;

    for (i = 0; i < MAX_OBJS_ATTRS; i++) {
        sessObject->attrList[i].attrib.pValue = NULL;
        sessObject->attrList[i].freeData = PR_FALSE;
    }
    sessObject->optimizeSpace = slot->optimizeSpace;

    object->handle = 0;
    object->next = object->prev = NULL;
    object->slot = slot;
    /* set up the validation flags */
    object->validation_value = 0;
    object->validation_attribute.next = NULL;
    object->validation_attribute.prev = NULL;
    object->validation_attribute.freeAttr = PR_FALSE;
    object->validation_attribute.freeData = PR_FALSE;
    object->validation_attribute.handle = CKA_OBJECT_VALIDATION_FLAGS;
    object->validation_attribute.attrib.type = CKA_OBJECT_VALIDATION_FLAGS;
    object->validation_attribute.attrib.pValue = &object->validation_value;
    object->validation_attribute.attrib.ulValueLen = sizeof(object->validation_value);
    /* initialize the FIPS flag properly */
    sftk_setFIPS(object, sftk_isFIPS(slot->slotID));
    object->source = SFTK_SOURCE_DEFAULT;

    object->refCount = 1;
    object->type = SFTK_SESSION_OBJECT_TYPE;
    sessObject->sessionList.next = NULL;
    sessObject->sessionList.prev = NULL;
    sessObject->sessionList.parent = object;
    sessObject->session = NULL;
    sessObject->wasDerived = PR_FALSE;
    if (!hasLocks)
        object->refLock = PR_NewLock();
    if (object->refLock == NULL) {
        PORT_Free(object);
        return NULL;
    }
    if (!hasLocks)
        sessObject->attributeLock = PR_NewLock();
    if (sessObject->attributeLock == NULL) {
        PR_DestroyLock(object->refLock);
        PORT_Free(object);
        return NULL;
    }
    for (i = 0; i < sessObject->hashSize; i++) {
        sessObject->head[i] = NULL;
    }
    object->objectInfo = NULL;
    object->infoFree = NULL;
    return object;
}

static CK_RV
sftk_DestroySessionObjectData(SFTKSessionObject *so)
{
    int i;

    for (i = 0; i < MAX_OBJS_ATTRS; i++) {
        SFTKAttribute *attribute = &so->attrList[i];
        unsigned char *value = attribute->attrib.pValue;
        if (value) {
            /* Aborts rather than zeroize or free through a wild pointer if
             * this object's memory was already destroyed and reused, or has
             * been corrupted. */
            PRBool ownsHeapValue = sftk_attributeOwnsHeapValue(so, attribute);
            PORT_Memset(value, 0, attribute->attrib.ulValueLen);
            if (ownsHeapValue) {
                PORT_Free(value);
            }
            attribute->attrib.pValue = NULL;
            attribute->attrib.ulValueLen = 0;
            attribute->freeData = PR_FALSE;
        }
    }
    /*  PR_DestroyLock(so->attributeLock);*/
    return CKR_OK;
}

/*
 * free all the data associated with an object. Object reference count must
 * be 'zero'.
 */
static CK_RV
sftk_DestroyObject(SFTKObject *object)
{
    CK_RV crv = CKR_OK;
    SFTKSessionObject *so = sftk_narrowToSessionObject(object);
    SFTKTokenObject *to = sftk_narrowToTokenObject(object);

    PORT_Assert(object->refCount == 0);

    /* delete the database value */
    if (to) {
        if (to->dbKey.data) {
            PORT_Free(to->dbKey.data);
            to->dbKey.data = NULL;
        }
    }
    if (so) {
        sftk_DestroySessionObjectData(so);
    }
    if (object->objectInfo) {
        (*object->infoFree)(object->objectInfo);
        object->objectInfo = NULL;
        object->infoFree = NULL;
    }
    if (so) {
        sftk_PutObjectToList(object, &sessionObjectList, PR_TRUE);
    } else {
        sftk_PutObjectToList(object, &tokenObjectList, PR_FALSE);
    }
    return crv;
}

void
sftk_ReferenceObject(SFTKObject *object)
{
    PR_Lock(object->refLock);
    /* A zero count means this object has already been destroyed; abort
     * before the new reference is used to operate on freed memory. */
    PORT_ReleaseAssert(object->refCount > 0);
    object->refCount++;
    PR_Unlock(object->refLock);
}

static SFTKObject *
sftk_ObjectFromHandleOnSlot(CK_OBJECT_HANDLE handle, SFTKSlot *slot)
{
    SFTKObject *object;
    PRUint32 index = sftk_hash(handle, slot->sessObjHashSize);

    if (sftk_isToken(handle)) {
        return sftk_NewTokenObject(slot, NULL, handle);
    }

    PR_Lock(slot->objectLock);
    sftkqueue_find2(object, handle, index, slot->sessObjHashTable);
    if (object) {
        sftk_ReferenceObject(object);
    }
    PR_Unlock(slot->objectLock);

    return (object);
}
/*
 * look up and object structure from a handle. OBJECT_Handles only make
 * sense in terms of a given session.  make a reference to that object
 * structure returned.
 */
SFTKObject *
sftk_ObjectFromHandle(CK_OBJECT_HANDLE handle, SFTKSession *session)
{
    SFTKSlot *slot = sftk_SlotFromSession(session);

    return sftk_ObjectFromHandleOnSlot(handle, slot);
}

/*
 * release a reference to an object handle
 */
SFTKFreeStatus
sftk_FreeObject(SFTKObject *object)
{
    PRBool destroy = PR_FALSE;
    CK_RV crv;

    PR_Lock(object->refLock);
    /* A zero count means this object has already been destroyed. Abort,
     * like an allocator that detects a double free, rather than tear the
     * object down a second time. */
    PORT_ReleaseAssert(object->refCount > 0);
    if (object->refCount == 1)
        destroy = PR_TRUE;
    object->refCount--;
    PR_Unlock(object->refLock);

    if (destroy) {
        crv = sftk_DestroyObject(object);
        if (crv != CKR_OK) {
            return SFTK_DestroyFailure;
        }
        return SFTK_Destroyed;
    }
    return SFTK_Busy;
}

/* find the next available object handle that isn't currently in use */
/* NOTE: This function could loop forever if we've exhausted all
 * 3^31-1 handles. This is highly unlikely (NSS has been running for
 * decades with this code) uless we start increasing the size of the
 * SFTK_TOKEN_MASK (which is just the high bit currently). */
CK_OBJECT_HANDLE
sftk_getNextHandle(SFTKSlot *slot)
{
    CK_OBJECT_HANDLE handle;
    SFTKObject *duplicateObject = NULL;
    do {
        PRUint32 wrappedAround;

        duplicateObject = NULL;
        PR_Lock(slot->objectLock);
        wrappedAround = slot->sessionObjectHandleCount & SFTK_TOKEN_MASK;
        handle = slot->sessionObjectHandleCount & ~SFTK_TOKEN_MASK;
        if (!handle) /* don't allow zero handle */
            handle = NSC_MIN_SESSION_OBJECT_HANDLE;
        slot->sessionObjectHandleCount = (handle + 1U) | wrappedAround;
        /* Is there already a session object with this handle? */
        if (wrappedAround) {
            sftkqueue_find(duplicateObject, handle, slot->sessObjHashTable,
                           slot->sessObjHashSize);
        }
        PR_Unlock(slot->objectLock);
    } while (duplicateObject != NULL);
    return handle;
}

/*
 * Claim the right to remove a session object from the slot's object hash.
 * Must be called with slot->objectLock held.
 *
 * Several threads can reach the same object: handles are slot-global, so any
 * session can resolve one through sftk_ObjectFromHandle, and
 * sftk_ClearSession claims removals too when the owning session is closed.
 * Only the thread that finds the object still queued unlinks it; without that,
 * each would unlink again and drop the queues' reference a second time.
 *
 * Returns PR_TRUE if this caller owns the removal, and with it the reference
 * the queues held. Drop that reference with sftk_FreeObject() after releasing
 * slot->objectLock.
 */
static PRBool
sftk_ClaimObjectRemovalLocked(SFTKSlot *slot, SFTKObject *object)
{
    PRUint32 index = sftk_hash(object->handle, slot->sessObjHashSize);

    if (!object->next && !object->prev &&
        slot->sessObjHashTable[index] != object) {
        return PR_FALSE;
    }
    sftkqueue_delete2(object, object->handle, index, slot->sessObjHashTable);
    /* sftkqueue_delete2 patches the neighbours but leaves object->next/prev
     * pointing at them. Clear them here so the next thread to take the lock
     * sees an unqueued object and does not re-claim the removal. */
    sftkqueue_clear_deleted_element(object);
    return PR_TRUE;
}

/*
 * add an object to the slot's object hash and to its session's object list
 */
void
sftk_AddObject(SFTKSession *session, SFTKObject *object)
{
    SFTKSlot *slot = sftk_SlotFromSession(session);
    SFTKSessionObject *so = sftk_narrowToSessionObject(object);
    PRUint32 index = sftk_hash(object->handle, slot->sessObjHashSize);

    /* the reference the queues hold, taken before the object is reachable */
    sftk_ReferenceObject(object);

    /* Both queues are linked under slot->objectLock in one critical section.
     * That gives the invariant the teardown paths depend on: a session object
     * is on its owning session's list if and only if it is in the slot's
     * object hash. See sftk_DeleteObject(). */
    sftkqueue_init_element(object);
    PR_Lock(slot->objectLock);
    if (so) {
        so->session = session;
        sftkqueue_add(&so->sessionList, 0, session->objects, 0);
    }
    sftkqueue_add2(object, object->handle, index, slot->sessObjHashTable);
    PR_Unlock(slot->objectLock);
}

/*
 * delete an object from a slot and session queue
 */
CK_RV
sftk_DeleteObject(SFTKSession *session, SFTKObject *object)
{
    SFTKSlot *slot = sftk_SlotFromSession(session);
    SFTKSessionObject *so = sftk_narrowToSessionObject(object);
    CK_RV crv = CKR_OK;

    /* Handle Token case */
    if (so && so->session) {
        PRBool ownsRemove;

        /* so->session is the session that created the object; it need not be
         * the caller's session, and we hold no reference on it. Winning the
         * claim is what makes it safe to touch: the object was still in the
         * slot hash, so by the invariant in sftk_AddObject() it is still on
         * so->session's list, so sftk_ClearSession() has not run for that
         * session and the session has not been freed. */
        PR_Lock(slot->objectLock);
        ownsRemove = sftk_ClaimObjectRemovalLocked(slot, object);
        if (ownsRemove) {
            PORT_Assert(sftkqueue_is_queued(&so->sessionList, 0,
                                            so->session->objects, 0));
            sftkqueue_delete(&so->sessionList, 0, so->session->objects, 0);
        }
        PR_Unlock(slot->objectLock);

        if (ownsRemove) {
            sftk_FreeObject(object); /* drop the queues' reference */
        }
    } else {
        SFTKDBHandle *handle = sftk_getDBForTokenObject(slot, object->handle);
#ifdef DEBUG
        SFTKTokenObject *to = sftk_narrowToTokenObject(object);
        PORT_Assert(to);
#endif
        crv = sftkdb_DestroyObject(handle, object->handle, object->objclass);
        sftk_freeDB(handle);
    }
    return crv;
}

/*
 * Token objects don't explicitly store their attributes, so we need to know
 * what attributes make up a particular token object before we can copy it.
 * below are the tables by object type.
 */
static const CK_ATTRIBUTE_TYPE commonAttrs[] = {
    CKA_CLASS, CKA_TOKEN, CKA_PRIVATE, CKA_LABEL, CKA_MODIFIABLE
};
static const CK_ULONG commonAttrsCount =
    sizeof(commonAttrs) / sizeof(commonAttrs[0]);

static const CK_ATTRIBUTE_TYPE commonKeyAttrs[] = {
    CKA_ID, CKA_START_DATE, CKA_END_DATE, CKA_DERIVE, CKA_LOCAL, CKA_KEY_TYPE
};
static const CK_ULONG commonKeyAttrsCount =
    sizeof(commonKeyAttrs) / sizeof(commonKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE secretKeyAttrs[] = {
    CKA_SENSITIVE, CKA_EXTRACTABLE, CKA_ENCRYPT, CKA_DECRYPT, CKA_SIGN,
    CKA_VERIFY, CKA_WRAP, CKA_UNWRAP, CKA_VALUE
};
static const CK_ULONG secretKeyAttrsCount =
    sizeof(secretKeyAttrs) / sizeof(secretKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE commonPubKeyAttrs[] = {
    CKA_ENCRYPT, CKA_VERIFY, CKA_VERIFY_RECOVER, CKA_WRAP, CKA_SUBJECT
};
static const CK_ULONG commonPubKeyAttrsCount =
    sizeof(commonPubKeyAttrs) / sizeof(commonPubKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE rsaPubKeyAttrs[] = {
    CKA_MODULUS, CKA_PUBLIC_EXPONENT
};
static const CK_ULONG rsaPubKeyAttrsCount =
    sizeof(rsaPubKeyAttrs) / sizeof(rsaPubKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE dsaPubKeyAttrs[] = {
    CKA_SUBPRIME, CKA_PRIME, CKA_BASE, CKA_VALUE
};
static const CK_ULONG dsaPubKeyAttrsCount =
    sizeof(dsaPubKeyAttrs) / sizeof(dsaPubKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE dhPubKeyAttrs[] = {
    CKA_PRIME, CKA_BASE, CKA_VALUE
};
static const CK_ULONG dhPubKeyAttrsCount =
    sizeof(dhPubKeyAttrs) / sizeof(dhPubKeyAttrs[0]);
static const CK_ATTRIBUTE_TYPE ecPubKeyAttrs[] = {
    CKA_EC_PARAMS, CKA_EC_POINT
};
static const CK_ULONG ecPubKeyAttrsCount =
    sizeof(ecPubKeyAttrs) / sizeof(ecPubKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE mldsaPubKeyAttrs[] = {
    CKA_PARAMETER_SET, CKA_VALUE
};
static const CK_ULONG mldsaPubKeyAttrsCount = PR_ARRAY_SIZE(mldsaPubKeyAttrs);

/* the vendor ML-KEM key types carry CKA_NSS_PARAMETER_SET rather than
 * CKA_PARAMETER_SET; list both and let the missing one be skipped */
static const CK_ATTRIBUTE_TYPE mlkemPubKeyAttrs[] = {
    CKA_PARAMETER_SET, CKA_NSS_PARAMETER_SET, CKA_VALUE
};
static const CK_ULONG mlkemPubKeyAttrsCount = PR_ARRAY_SIZE(mlkemPubKeyAttrs);

static const CK_ATTRIBUTE_TYPE commonPrivKeyAttrs[] = {
    CKA_DECRYPT, CKA_SIGN, CKA_SIGN_RECOVER, CKA_UNWRAP, CKA_SUBJECT,
    CKA_SENSITIVE, CKA_EXTRACTABLE, CKA_NSS_DB, CKA_PUBLIC_KEY_INFO
};
static const CK_ULONG commonPrivKeyAttrsCount =
    sizeof(commonPrivKeyAttrs) / sizeof(commonPrivKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE rsaPrivKeyAttrs[] = {
    CKA_MODULUS, CKA_PUBLIC_EXPONENT, CKA_PRIVATE_EXPONENT,
    CKA_PRIME_1, CKA_PRIME_2, CKA_EXPONENT_1, CKA_EXPONENT_2, CKA_COEFFICIENT
};
static const CK_ULONG rsaPrivKeyAttrsCount =
    sizeof(rsaPrivKeyAttrs) / sizeof(rsaPrivKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE dsaPrivKeyAttrs[] = {
    CKA_SUBPRIME, CKA_PRIME, CKA_BASE, CKA_VALUE
};
static const CK_ULONG dsaPrivKeyAttrsCount =
    sizeof(dsaPrivKeyAttrs) / sizeof(dsaPrivKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE dhPrivKeyAttrs[] = {
    CKA_PRIME, CKA_BASE, CKA_VALUE
};
static const CK_ULONG dhPrivKeyAttrsCount =
    sizeof(dhPrivKeyAttrs) / sizeof(dhPrivKeyAttrs[0]);
static const CK_ATTRIBUTE_TYPE ecPrivKeyAttrs[] = {
    CKA_EC_PARAMS, CKA_VALUE
};
static const CK_ULONG ecPrivKeyAttrsCount =
    sizeof(ecPrivKeyAttrs) / sizeof(ecPrivKeyAttrs[0]);

static const CK_ATTRIBUTE_TYPE mldsaPrivKeyAttrs[] = {
    CKA_PARAMETER_SET, CKA_VALUE, CKA_SEED
};
static const CK_ULONG mldsaPrivKeyAttrsCount = PR_ARRAY_SIZE(mldsaPrivKeyAttrs);

static const CK_ATTRIBUTE_TYPE mlkemPrivKeyAttrs[] = {
    CKA_PARAMETER_SET, CKA_NSS_PARAMETER_SET, CKA_VALUE, CKA_SEED
};
static const CK_ULONG mlkemPrivKeyAttrsCount = PR_ARRAY_SIZE(mlkemPrivKeyAttrs);

static const CK_ATTRIBUTE_TYPE certAttrs[] = {
    CKA_CERTIFICATE_TYPE, CKA_VALUE, CKA_SUBJECT, CKA_ISSUER, CKA_SERIAL_NUMBER
};
static const CK_ULONG certAttrsCount =
    sizeof(certAttrs) / sizeof(certAttrs[0]);

static const CK_ATTRIBUTE_TYPE nssTrustAttrs[] = {
    CKA_ISSUER, CKA_SERIAL_NUMBER, CKA_NSS_CERT_SHA1_HASH,
    CKA_NSS_CERT_MD5_HASH, CKA_NSS_TRUST_SERVER_AUTH, CKA_NSS_TRUST_CLIENT_AUTH,
    CKA_NSS_TRUST_EMAIL_PROTECTION, CKA_NSS_TRUST_CODE_SIGNING,
    CKA_NSS_TRUST_STEP_UP_APPROVED
};
static const CK_ULONG nssTrustAttrsCount =
    sizeof(nssTrustAttrs) / sizeof(nssTrustAttrs[0]);

static const CK_ATTRIBUTE_TYPE trustAttrs[] = {
    CKA_ISSUER, CKA_SERIAL_NUMBER, CKA_HASH_OF_CERTIFICATE,
    CKA_NAME_HASH_ALGORITHM, CKA_PKCS_TRUST_SERVER_AUTH,
    CKA_PKCS_TRUST_CLIENT_AUTH, CKA_PKCS_TRUST_EMAIL_PROTECTION,
    CKA_PKCS_TRUST_CODE_SIGNING
};
static const CK_ULONG trustAttrsCount =
    sizeof(trustAttrs) / sizeof(trustAttrs[0]);

static const CK_ATTRIBUTE_TYPE smimeAttrs[] = {
    CKA_SUBJECT, CKA_NSS_EMAIL, CKA_NSS_SMIME_TIMESTAMP, CKA_VALUE
};
static const CK_ULONG smimeAttrsCount =
    sizeof(smimeAttrs) / sizeof(smimeAttrs[0]);

static const CK_ATTRIBUTE_TYPE crlAttrs[] = {
    CKA_SUBJECT, CKA_VALUE, CKA_NSS_URL, CKA_NSS_KRL
};
static const CK_ULONG crlAttrsCount =
    sizeof(crlAttrs) / sizeof(crlAttrs[0]);

/* copy an object based on it's table */
CK_RV
stfk_CopyTokenAttributes(SFTKObject *destObject, SFTKTokenObject *src_to,
                         const CK_ATTRIBUTE_TYPE *attrArray, CK_ULONG attrCount)
{
    SFTKAttribute *attribute;
    SFTKAttribute *newAttribute;
    CK_RV crv = CKR_OK;
    unsigned int i;

    for (i = 0; i < attrCount; i++) {
        if (!sftk_hasAttribute(destObject, attrArray[i])) {
            attribute = sftk_FindAttribute(&src_to->obj, attrArray[i]);
            if (!attribute) {
                continue; /* return CKR_ATTRIBUTE_VALUE_INVALID; */
            }
            /* we need to copy the attribute since each attribute
             * only has one set of link list pointers */
            newAttribute = sftk_NewAttribute(destObject,
                                             sftk_attr_expand(&attribute->attrib));
            sftk_FreeAttribute(attribute); /* free the old attribute */
            if (!newAttribute) {
                return CKR_HOST_MEMORY;
            }
            sftk_AddAttribute(destObject, newAttribute);
        }
    }
    return crv;
}

CK_RV
stfk_CopyTokenPrivateKey(SFTKObject *destObject, SFTKTokenObject *src_to)
{
    CK_RV crv;
    CK_KEY_TYPE key_type;
    SFTKAttribute *attribute;

    /* copy the common attributes for all keys first */
    crv = stfk_CopyTokenAttributes(destObject, src_to, commonKeyAttrs,
                                   commonKeyAttrsCount);
    if (crv != CKR_OK) {
        goto fail;
    }
    /* copy the common attributes for all private keys next */
    crv = stfk_CopyTokenAttributes(destObject, src_to, commonPrivKeyAttrs,
                                   commonPrivKeyAttrsCount);
    if (crv != CKR_OK) {
        goto fail;
    }
    attribute = sftk_FindAttribute(&src_to->obj, CKA_KEY_TYPE);
    PORT_Assert(attribute); /* if it wasn't here, ww should have failed
                             * copying the common attributes */
    if (!attribute) {
        /* OK, so CKR_ATTRIBUTE_VALUE_INVALID is the immediate error, but
         * the fact is, the only reason we couldn't get the attribute would
         * be a memory error or database error (an error in the 'device').
         * if we have a database error code, we could return it here */
        crv = CKR_DEVICE_ERROR;
        goto fail;
    }
    key_type = *(CK_KEY_TYPE *)attribute->attrib.pValue;
    sftk_FreeAttribute(attribute);

    /* finally copy the attributes for various private key types */
    switch (key_type) {
        case CKK_RSA:
            crv = stfk_CopyTokenAttributes(destObject, src_to, rsaPrivKeyAttrs,
                                           rsaPrivKeyAttrsCount);
            break;
        case CKK_DSA:
            crv = stfk_CopyTokenAttributes(destObject, src_to, dsaPrivKeyAttrs,
                                           dsaPrivKeyAttrsCount);
            break;
        case CKK_ML_DSA:
            crv = stfk_CopyTokenAttributes(destObject, src_to, mldsaPrivKeyAttrs,
                                           mldsaPrivKeyAttrsCount);
            break;
        case CKK_NSS_ML_KEM:
        case CKK_ML_KEM:
            crv = stfk_CopyTokenAttributes(destObject, src_to, mlkemPrivKeyAttrs,
                                           mlkemPrivKeyAttrsCount);
            break;
        case CKK_DH:
            crv = stfk_CopyTokenAttributes(destObject, src_to, dhPrivKeyAttrs,
                                           dhPrivKeyAttrsCount);
            break;
        case CKK_EC:
        case CKK_EC_EDWARDS:
        case CKK_EC_MONTGOMERY:
            crv = stfk_CopyTokenAttributes(destObject, src_to, ecPrivKeyAttrs,
                                           ecPrivKeyAttrsCount);
            break;
        default:
            crv = CKR_DEVICE_ERROR; /* shouldn't happen unless we store more types
                                     * of token keys into our database. */
    }
fail:
    return crv;
}

CK_RV
stfk_CopyTokenPublicKey(SFTKObject *destObject, SFTKTokenObject *src_to)
{
    CK_RV crv;
    CK_KEY_TYPE key_type;
    SFTKAttribute *attribute;

    /* copy the common attributes for all keys first */
    crv = stfk_CopyTokenAttributes(destObject, src_to, commonKeyAttrs,
                                   commonKeyAttrsCount);
    if (crv != CKR_OK) {
        goto fail;
    }

    /* copy the common attributes for all public keys next */
    crv = stfk_CopyTokenAttributes(destObject, src_to, commonPubKeyAttrs,
                                   commonPubKeyAttrsCount);
    if (crv != CKR_OK) {
        goto fail;
    }
    attribute = sftk_FindAttribute(&src_to->obj, CKA_KEY_TYPE);
    PORT_Assert(attribute); /* if it wasn't here, ww should have failed
                             * copying the common attributes */
    if (!attribute) {
        /* OK, so CKR_ATTRIBUTE_VALUE_INVALID is the immediate error, but
         * the fact is, the only reason we couldn't get the attribute would
         * be a memory error or database error (an error in the 'device').
         * if we have a database error code, we could return it here */
        crv = CKR_DEVICE_ERROR;
        goto fail;
    }
    key_type = *(CK_KEY_TYPE *)attribute->attrib.pValue;
    sftk_FreeAttribute(attribute);

    /* finally copy the attributes for various public key types */
    switch (key_type) {
        case CKK_RSA:
            crv = stfk_CopyTokenAttributes(destObject, src_to, rsaPubKeyAttrs,
                                           rsaPubKeyAttrsCount);
            break;
        case CKK_DSA:
            crv = stfk_CopyTokenAttributes(destObject, src_to, dsaPubKeyAttrs,
                                           dsaPubKeyAttrsCount);
            break;
        case CKK_ML_DSA:
            crv = stfk_CopyTokenAttributes(destObject, src_to, mldsaPubKeyAttrs,
                                           mldsaPubKeyAttrsCount);
            break;
        case CKK_NSS_ML_KEM:
        case CKK_ML_KEM:
            crv = stfk_CopyTokenAttributes(destObject, src_to, mlkemPubKeyAttrs,
                                           mlkemPubKeyAttrsCount);
            break;
        case CKK_DH:
            crv = stfk_CopyTokenAttributes(destObject, src_to, dhPubKeyAttrs,
                                           dhPubKeyAttrsCount);
            break;
        case CKK_EC:
        case CKK_EC_EDWARDS:
        case CKK_EC_MONTGOMERY:
            crv = stfk_CopyTokenAttributes(destObject, src_to, ecPubKeyAttrs,
                                           ecPubKeyAttrsCount);
            break;
        default:
            crv = CKR_DEVICE_ERROR; /* shouldn't happen unless we store more types
                                     * of token keys into our database. */
    }
fail:
    return crv;
}
CK_RV
stfk_CopyTokenSecretKey(SFTKObject *destObject, SFTKTokenObject *src_to)
{
    CK_RV crv;
    crv = stfk_CopyTokenAttributes(destObject, src_to, commonKeyAttrs,
                                   commonKeyAttrsCount);
    if (crv != CKR_OK) {
        goto fail;
    }
    crv = stfk_CopyTokenAttributes(destObject, src_to, secretKeyAttrs,
                                   secretKeyAttrsCount);
fail:
    return crv;
}

/*
 * Copy a token object. We need to explicitly copy the relevant
 * attributes since token objects don't store those attributes in
 * the token itself.
 */
CK_RV
sftk_CopyTokenObject(SFTKObject *destObject, SFTKObject *srcObject)
{
    SFTKTokenObject *src_to = sftk_narrowToTokenObject(srcObject);
    CK_RV crv;

    PORT_Assert(src_to);
    if (src_to == NULL) {
        return CKR_DEVICE_ERROR; /* internal state inconsistant */
    }

    crv = stfk_CopyTokenAttributes(destObject, src_to, commonAttrs,
                                   commonAttrsCount);
    if (crv != CKR_OK) {
        goto fail;
    }
    switch (src_to->obj.objclass) {
        case CKO_CERTIFICATE:
            crv = stfk_CopyTokenAttributes(destObject, src_to, certAttrs,
                                           certAttrsCount);
            break;
        case CKO_NSS_TRUST:
            crv = stfk_CopyTokenAttributes(destObject, src_to, nssTrustAttrs,
                                           nssTrustAttrsCount);
            break;
        case CKO_TRUST:
            crv = stfk_CopyTokenAttributes(destObject, src_to, trustAttrs,
                                           trustAttrsCount);
            break;
        case CKO_NSS_SMIME:
            crv = stfk_CopyTokenAttributes(destObject, src_to, smimeAttrs,
                                           smimeAttrsCount);
            break;
        case CKO_NSS_CRL:
            crv = stfk_CopyTokenAttributes(destObject, src_to, crlAttrs,
                                           crlAttrsCount);
            break;
        case CKO_PRIVATE_KEY:
            crv = stfk_CopyTokenPrivateKey(destObject, src_to);
            break;
        case CKO_PUBLIC_KEY:
            crv = stfk_CopyTokenPublicKey(destObject, src_to);
            break;
        case CKO_SECRET_KEY:
            crv = stfk_CopyTokenSecretKey(destObject, src_to);
            break;
        default:
            crv = CKR_DEVICE_ERROR; /* shouldn't happen unless we store more types
                                     * of token keys into our database. */
    }
fail:
    return crv;
}

/*
 * copy the attributes from one object to another. Don't overwrite existing
 * attributes. NOTE: This is a pretty expensive operation since it
 * grabs the attribute locks for the src object for a *long* time.
 */
CK_RV
sftk_CopyObject(SFTKObject *destObject, SFTKObject *srcObject)
{
    SFTKAttribute *attribute;
    SFTKSessionObject *src_so = sftk_narrowToSessionObject(srcObject);
    SFTKSessionObject *dest_so = sftk_narrowToSessionObject(destObject);
    unsigned int i;

    destObject->validation_value = srcObject->validation_value;
    destObject->source = srcObject->source;
    if (src_so == NULL) {
        return sftk_CopyTokenObject(destObject, srcObject);
    }
    PORT_Assert(dest_so != NULL);
    if (dest_so == NULL) {
        return CKR_ARGUMENTS_BAD;
    }

    PR_Lock(src_so->attributeLock);
    for (i = 0; i < src_so->hashSize; i++) {
        attribute = src_so->head[i];
        do {
            if (attribute) {
                SFTKAttribute *existing;
                sftkqueue_find(existing, attribute->handle,
                               dest_so->head, dest_so->hashSize);
                if (!existing) {
                    /* we need to copy the attribute since each attribute
                     * only has one set of link list pointers */
                    SFTKAttribute *newAttribute = sftk_InitAttribute(
                        dest_so, dest_so->nextAttr++,
                        sftk_attr_expand(&attribute->attrib));
                    if (newAttribute == NULL) {
                        PR_Unlock(src_so->attributeLock);
                        return CKR_HOST_MEMORY;
                    }
                    sftkqueue_add(newAttribute, newAttribute->handle,
                                  dest_so->head, dest_so->hashSize);
                }
                attribute = attribute->next;
            }
        } while (attribute != NULL);
    }
    PR_Unlock(src_so->attributeLock);

    return CKR_OK;
}

/*
 * ******************** Search Utilities *******************************
 */

/* add an object to a search list */
CK_RV
AddToList(SFTKObjectListElement **list, SFTKObject *object)
{
    SFTKObjectListElement *newElem =
        (SFTKObjectListElement *)PORT_Alloc(sizeof(SFTKObjectListElement));

    if (newElem == NULL)
        return CKR_HOST_MEMORY;

    newElem->next = *list;
    newElem->object = object;
    sftk_ReferenceObject(object);

    *list = newElem;
    return CKR_OK;
}

/* return true if the object matches the template */
PRBool
sftk_objectMatch(SFTKObject *object, CK_ATTRIBUTE_PTR theTemplate, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        SFTKAttribute *attribute = sftk_FindAttribute(object, theTemplate[i].type);
        if (attribute == NULL) {
            return PR_FALSE;
        }
        if (attribute->attrib.ulValueLen == theTemplate[i].ulValueLen) {
            if (PORT_Memcmp(attribute->attrib.pValue, theTemplate[i].pValue,
                            theTemplate[i].ulValueLen) == 0) {
                sftk_FreeAttribute(attribute);
                continue;
            }
        }
        sftk_FreeAttribute(attribute);
        return PR_FALSE;
    }
    return PR_TRUE;
}

/* search through all the objects in the queue and return the template matches
 * in the object list.
 */
CK_RV
sftk_searchObjectList(SFTKSearchResults *search, SFTKObject **head,
                      unsigned int size, PRLock *lock, CK_ATTRIBUTE_PTR theTemplate,
                      int count, PRBool isLoggedIn)
{
    unsigned int i;
    SFTKObject *object;
    CK_RV crv = CKR_OK;

    PR_Lock(lock);
    for (i = 0; i < size; i++) {
        for (object = head[i]; object != NULL; object = object->next) {
            if (sftk_objectMatch(object, theTemplate, count)) {
                /* don't return objects that aren't yet visible */
                if ((!isLoggedIn) && sftk_isTrue(object, CKA_PRIVATE))
                    continue;
                sftk_addHandle(search, object->handle);
            }
        }
    }
    PR_Unlock(lock);
    return crv;
}

/*
 * free a single list element. Return the Next object in the list.
 */
SFTKObjectListElement *
sftk_FreeObjectListElement(SFTKObjectListElement *objectList)
{
    SFTKObjectListElement *ol = objectList->next;

    sftk_FreeObject(objectList->object);
    PORT_Free(objectList);
    return ol;
}

/* free an entire object list */
void
sftk_FreeObjectList(SFTKObjectListElement *objectList)
{
    SFTKObjectListElement *ol;

    for (ol = objectList; ol != NULL; ol = sftk_FreeObjectListElement(ol)) {
    }
}

/*
 * free a search structure
 */
void
sftk_FreeSearch(SFTKSearchResults *search)
{
    if (search->handles) {
        PORT_Free(search->handles);
    }
    PORT_Free(search);
}

/*
 * ******************** Session Utilities *******************************
 */

/* update the sessions state based in it's flags and wether or not it's
 * logged in */
void
sftk_update_state(SFTKSlot *slot, SFTKSession *session)
{
    PR_Lock(slot->slotLock);
    if (slot->isLoggedIn) {
        if (slot->ssoLoggedIn) {
            session->info.state = CKS_RW_SO_FUNCTIONS;
        } else if (session->info.flags & CKF_RW_SESSION) {
            session->info.state = CKS_RW_USER_FUNCTIONS;
        } else {
            session->info.state = CKS_RO_USER_FUNCTIONS;
        }
    } else {
        if (session->info.flags & CKF_RW_SESSION) {
            session->info.state = CKS_RW_PUBLIC_SESSION;
        } else {
            session->info.state = CKS_RO_PUBLIC_SESSION;
        }
    }
    PR_Unlock(slot->slotLock);
}

/* update the state of all the sessions on a slot */
void
sftk_update_all_states(SFTKSlot *slot)
{
    unsigned int i;
    SFTKSession *session;

    for (i = 0; i < slot->sessHashSize; i++) {
        PRLock *lock = SFTK_HEAD_BUCKET_LOCK(slot, i);
        PR_Lock(lock);
        for (session = slot->head[i]; session; session = session->next) {
            sftk_update_state(slot, session);
        }
        PR_Unlock(lock);
    }
}

/*
 * context are cipher and digest contexts that are associated with a session
 */
void
sftk_FreeContext(SFTKSessionContext *context)
{
    if (context->cipherInfo) {
        (*context->destroy)(context->cipherInfo, PR_TRUE);
    }
    if (context->hashInfo) {
        (*context->hashdestroy)(context->hashInfo, PR_TRUE);
    }
    if (context->key) {
        sftk_FreeObject(context->key);
        context->key = NULL;
    }
    if (context->signature) {
        SECITEM_FreeItem(context->signature, PR_TRUE);
        context->signature = NULL;
    }
    PORT_Free(context);
}

/*
 * Init a new session. NOTE: The session handle is not set, and the
 * session is not added to the slot's session queue.
 */
CK_RV
sftk_InitSession(SFTKSession *session, SFTKSlot *slot, CK_SLOT_ID slotID,
                 CK_NOTIFY notify, CK_VOID_PTR pApplication, CK_FLAGS flags)
{
    session->next = session->prev = NULL;
    session->refCount = 1;
    session->enc_context = NULL;
    session->hash_context = NULL;
    session->search = NULL;
    session->objectIDCount = 1;
    /* session->objects[] is protected by slot->objectLock, not by a
     * per-session lock -- see the lock notes on SFTKSlotStr. */
    session->objects[0] = NULL;

    session->slot = slot;
    session->notify = notify;
    session->appData = pApplication;
    session->info.flags = flags;
    session->info.slotID = slotID;
    session->info.ulDeviceError = 0;
    sftk_update_state(slot, session);
    /* no ops completed yet, so the last one couldn't be a FIPS op */
    session->lastOpWasFIPS = PR_FALSE;
    return CKR_OK;
}

/*
 * Create a new session and init it.
 */
SFTKSession *
sftk_NewSession(CK_SLOT_ID slotID, CK_NOTIFY notify, CK_VOID_PTR pApplication,
                CK_FLAGS flags)
{
    SFTKSession *session;
    SFTKSlot *slot = sftk_SlotFromID(slotID, PR_FALSE);
    CK_RV crv;

    if (slot == NULL)
        return NULL;

    session = (SFTKSession *)PORT_Alloc(sizeof(SFTKSession));
    if (session == NULL)
        return NULL;

    crv = sftk_InitSession(session, slot, slotID, notify, pApplication, flags);
    if (crv != CKR_OK) {
        PORT_Free(session);
        return NULL;
    }
    return session;
}

/* free all the data associated with a session. */
void
sftk_ClearSession(SFTKSession *session)
{
    SFTKSlot *slot = sftk_SlotFromSession(session);
    SFTKObjectList *op, *next;
    SFTKObjectList *toFree = NULL;

    /* Detach every object this session still owns.
     *
     * No one references the session any more, but that does not make it safe
     * to walk this chain unlocked: object handles are slot-global, so a
     * C_DestroyObject on any other session may be removing one of these
     * objects right now. slot->objectLock arbitrates both queues and belongs
     * to the slot, which outlives every session on it.
     */
    PR_Lock(slot->objectLock);
    for (op = session->objects[0]; op != NULL; op = next) {
        next = op->next;
        /* Unlink by hand: we are discarding the whole list, so there is no
         * point patching neighbours. */
        op->next = op->prev = NULL;
        /* Everything on this list is still in the slot hash -- a claimant
         * unlinks it from both under this lock -- so the claim normally
         * succeeds. The test guards against a path that ever removes an
         * object from only one queue. */
        if (sftk_ClaimObjectRemovalLocked(slot, op->parent)) {
            /* We own the queues' reference, so the object stays alive until
             * we drop it; chain the unlinked element up and free below. */
            op->next = toFree;
            toFree = op;
        }
    }
    session->objects[0] = NULL;
    PR_Unlock(slot->objectLock);

    /* An object that outlives us -- because another thread still holds a
     * reference -- keeps a stale so->session pointing here. That is safe:
     * so->session is only dereferenced under slot->objectLock by a thread
     * that has just won the removal claim, and the claim can no longer
     * succeed for an object we have already pulled out of the slot hash. */
    for (op = toFree; op != NULL; op = next) {
        next = op->next;
        op->next = NULL;
        sftk_FreeObject(op->parent); /* drop the queues' reference */
    }

    if (session->enc_context) {
        sftk_FreeContext(session->enc_context);
        session->enc_context = NULL;
    }
    if (session->hash_context) {
        sftk_FreeContext(session->hash_context);
        session->hash_context = NULL;
    }
    if (session->search) {
        sftk_FreeSearch(session->search);
        session->search = NULL;
    }
}

/* free the data associated with the session, and the session.
 * Caller must have driven refCount to 0; reachable only via sftk_FreeSession. */
static void
sftk_DestroySession(SFTKSession *session)
{
    PORT_ReleaseAssert(session->refCount == 0);
    sftk_ClearSession(session);
    PORT_Free(session);
}

/*
 * look up a session structure from a session handle
 * generate a reference to it.
 */
SFTKSession *
sftk_SessionFromHandle(CK_SESSION_HANDLE handle)
{
    SFTKSlot *slot = sftk_SlotFromSessionHandle(handle);
    SFTKSession *session;
    PRLock *lock;

    if (!slot)
        return NULL;
    lock = SFTK_SESSION_LOCK(slot, handle);

    PR_Lock(lock);
    sftkqueue_find(session, handle, slot->head, slot->sessHashSize);
    if (session)
        session->refCount++;
    PR_Unlock(lock);

    return (session);
}

/*
 * release a reference to a session. The slot's session bucket holds the
 * initial reference (created by sftk_InitSession); sftk_SessionFromHandle
 * adds another for the duration of the caller's use. NSC_CloseSession and
 * sftk_CloseAllSessions drop the bucket's reference once the session has
 * been removed from the queue. The thread that drives refCount to 0
 * destroys the session.
 */
void
sftk_FreeSession(SFTKSession *session)
{
    PRBool destroy = PR_FALSE;
    SFTKSlot *slot = sftk_SlotFromSession(session);
    PRLock *lock = SFTK_SESSION_LOCK(slot, session->handle);

    PR_Lock(lock);
    PORT_ReleaseAssert(session->refCount > 0);
    if (session->refCount == 1)
        destroy = PR_TRUE;
    session->refCount--;
    PR_Unlock(lock);

    if (destroy)
        sftk_DestroySession(session);
}

void
sftk_addHandle(SFTKSearchResults *search, CK_OBJECT_HANDLE handle)
{
    if (search->handles == NULL) {
        return;
    }
    if (search->size >= search->array_size) {
        search->array_size += NSC_SEARCH_BLOCK_SIZE;
        search->handles = (CK_OBJECT_HANDLE *)PORT_Realloc(search->handles,
                                                           sizeof(CK_OBJECT_HANDLE) * search->array_size);
        if (search->handles == NULL) {
            return;
        }
    }
    search->handles[search->size] = handle;
    search->size++;
}

static CK_RV
handleToClass(SFTKSlot *slot, CK_OBJECT_HANDLE handle,
              CK_OBJECT_CLASS *objClass)
{
    SFTKDBHandle *dbHandle = sftk_getDBForTokenObject(slot, handle);
    CK_ATTRIBUTE objClassTemplate;
    CK_RV crv;

    *objClass = CKO_DATA;
    objClassTemplate.type = CKA_CLASS;
    objClassTemplate.pValue = objClass;
    objClassTemplate.ulValueLen = sizeof(*objClass);
    crv = sftkdb_GetAttributeValue(dbHandle, handle, &objClassTemplate, 1);
    sftk_freeDB(dbHandle);
    return crv;
}

SFTKObject *
sftk_NewTokenObject(SFTKSlot *slot, SECItem *dbKey, CK_OBJECT_HANDLE handle)
{
    SFTKObject *object = NULL;
    PRBool hasLocks = PR_FALSE;
    CK_RV crv;

    object = sftk_GetObjectFromList(&hasLocks, PR_FALSE, &tokenObjectList, 0,
                                    PR_FALSE);
    if (object == NULL) {
        return NULL;
    }

    object->handle = handle;
    /* every object must have a class, if we can't get it, the object
     * doesn't exist */
    crv = handleToClass(slot, handle, &object->objclass);
    if (crv != CKR_OK) {
        goto loser;
    }
    object->slot = slot;
    /* set up the validation flags */
    object->validation_value = 0;
    object->validation_attribute.next = NULL;
    object->validation_attribute.prev = NULL;
    object->validation_attribute.freeAttr = PR_FALSE;
    object->validation_attribute.freeData = PR_FALSE;
    object->validation_attribute.handle = CKA_OBJECT_VALIDATION_FLAGS;
    object->validation_attribute.attrib.type = CKA_OBJECT_VALIDATION_FLAGS;
    object->validation_attribute.attrib.pValue = &object->validation_value;
    object->validation_attribute.attrib.ulValueLen = sizeof(object->validation_value);
    /* initialize the FIPS flag properly */
    sftk_setFIPS(object, sftk_isFIPS(slot->slotID));
    object->source = SFTK_SOURCE_DEFAULT;
    object->objectInfo = NULL;
    object->infoFree = NULL;
    if (!hasLocks) {
        object->refLock = PR_NewLock();
    }
    if (object->refLock == NULL) {
        goto loser;
    }
    object->refCount = 1;
    object->type = SFTK_TOKEN_OBJECT_TYPE;

    return object;
loser:
    (void)sftk_DestroyObject(object);
    return NULL;
}

CK_RV
sftk_convertSessionToToken(SFTKObject *obj)
{
    SECItem *dbKey;
    SFTKSessionObject *so = (SFTKSessionObject *)obj;
    SFTKTokenObject *to = sftk_narrowToTokenObject(obj);
    CK_RV crv;

    sftk_DestroySessionObjectData(so);
    PR_DestroyLock(so->attributeLock);

    sftk_tokenKeyLock(so->obj.slot);
    dbKey = sftk_lookupTokenKeyByHandle(so->obj.slot, so->obj.handle);
    if (dbKey && SECFailure == SECITEM_CopyItem(NULL, &to->dbKey, dbKey)) {
        crv = CKR_HOST_MEMORY;
    } else {
        crv = CKR_OK;
    }
    sftk_tokenKeyUnlock(so->obj.slot);
    return crv;
}

SFTKSessionObject *
sftk_narrowToSessionObject(SFTKObject *obj)
{
    PRBool handleSaysSession = !sftk_isToken(obj->handle);
    PRBool typeSaysSession = obj->type == SFTK_SESSION_OBJECT_TYPE;
    if (handleSaysSession != typeSaysSession) {
        PORT_Assert(0);
        return NULL;
    }
    return handleSaysSession ? (SFTKSessionObject *)obj : NULL;
}

SFTKTokenObject *
sftk_narrowToTokenObject(SFTKObject *obj)
{
    PRBool handleSaysToken = sftk_isToken(obj->handle);
    PRBool typeSaysToken = obj->type == SFTK_TOKEN_OBJECT_TYPE;
    if (handleSaysToken != typeSaysToken) {
        PORT_Assert(0);
        return NULL;
    }
    return handleSaysToken ? (SFTKTokenObject *)obj : NULL;
}

/* Constant time helper functions */

/* sftk_CKRVToMask returns, in constant time, a mask value of
 * all ones if rv == CKR_OK.  Otherwise it returns zero. */
unsigned int
sftk_CKRVToMask(CK_RV rv)
{
    PR_STATIC_ASSERT(CKR_OK == 0);
    return ~PORT_CT_NOT_ZERO(rv);
}

/* sftk_CheckCBCPadding checks, in constant time, the padding validity and
 * accordingly sets the pad length. */
CK_RV
sftk_CheckCBCPadding(CK_BYTE_PTR pBuf, unsigned int bufLen,
                     unsigned int blockSize, unsigned int *outPadSize)
{
    PORT_Assert(outPadSize);

    /* CBC-padded plaintext is always at least one full block */
    if (bufLen < blockSize || blockSize == 0) {
        *outPadSize = 0;
        return CKR_ENCRYPTED_DATA_INVALID;
    }

    unsigned int padSize = (unsigned int)pBuf[bufLen - 1];

    /* If padSize <= blockSize, set goodPad to all-1s and all-0s otherwise.*/
    unsigned int goodPad = PORT_CT_DUPLICATE_MSB_TO_ALL(~(blockSize - padSize));
    /* padSize should not be 0 */
    goodPad &= PORT_CT_NOT_ZERO(padSize);

    unsigned int i;
    for (i = 0; i < blockSize; i++) {
        /* If i < padSize, set loopMask to all-1s and all-0s otherwise.*/
        unsigned int loopMask = PORT_CT_DUPLICATE_MSB_TO_ALL(~(padSize - 1 - i));
        /* Get the padding value (should be padSize) from buffer */
        unsigned int padVal = pBuf[bufLen - 1 - i];
        /* Update goodPad only if i < padSize */
        goodPad &= PORT_CT_SEL(loopMask, ~(padVal ^ padSize), goodPad);
    }

    /* If any of the final padding bytes had the wrong value, one or more
     * of the lower eight bits of |goodPad| will be cleared. We AND the
     * bottom 8 bits together and duplicate the result to all the bits. */
    goodPad &= goodPad >> 4;
    goodPad &= goodPad >> 2;
    goodPad &= goodPad >> 1;
    goodPad <<= sizeof(goodPad) * 8 - 1;
    goodPad = PORT_CT_DUPLICATE_MSB_TO_ALL(goodPad);

    /* Set outPadSize to padSize or 0 */
    *outPadSize = PORT_CT_SEL(goodPad, padSize, 0);
    /* Return OK if the pad is valid */
    return PORT_CT_SEL(goodPad, CKR_OK, CKR_ENCRYPTED_DATA_INVALID);
}

void
sftk_EncodeInteger(PRUint64 integer, CK_ULONG num_bits, CK_BBOOL littleEndian,
                   CK_BYTE_PTR output, CK_ULONG_PTR output_len)
{
    if (output_len) {
        *output_len = (num_bits / 8);
    }

    PR_ASSERT(num_bits > 0 && num_bits <= 64 && (num_bits % 8) == 0);

    if (littleEndian == CK_TRUE) {
        for (size_t offset = 0; offset < num_bits / 8; offset++) {
            output[offset] = (unsigned char)((integer >> (offset * 8)) & 0xFF);
        }
    } else {
        for (size_t offset = 0; offset < num_bits / 8; offset++) {
            PRUint64 shift = num_bits - (offset + 1) * 8;
            output[offset] = (unsigned char)((integer >> shift) & 0xFF);
        }
    }
}

CK_FLAGS
sftk_AttributeToFlags(CK_ATTRIBUTE_TYPE op)
{
    CK_FLAGS flags = 0;

    switch (op) {
        case CKA_ENCRYPT:
            flags = CKF_ENCRYPT;
            break;
        case CKA_DECRYPT:
            flags = CKF_DECRYPT;
            break;
        case CKA_WRAP:
            flags = CKF_WRAP;
            break;
        case CKA_UNWRAP:
            flags = CKF_UNWRAP;
            break;
        case CKA_SIGN:
            flags = CKF_SIGN;
            break;
        case CKA_SIGN_RECOVER:
            flags = CKF_SIGN_RECOVER;
            break;
        case CKA_VERIFY:
            flags = CKF_VERIFY;
            break;
        case CKA_VERIFY_RECOVER:
            flags = CKF_VERIFY_RECOVER;
            break;
        case CKA_DERIVE:
            flags = CKF_DERIVE;
            break;
        /* fake attribute to select digesting */
        case CKA_DIGEST:
            flags = CKF_DIGEST;
            break;
        /* fake attribute to select key gen */
        case CKA_NSS_GENERATE:
            flags = CKF_GENERATE;
            break;
        /* fake attribute to select key pair gen */
        case CKA_NSS_GENERATE_KEY_PAIR:
            flags = CKF_GENERATE_KEY_PAIR;
            break;
        /* fake attributes to to handle MESSAGE* flags */
        case CKA_NSS_MESSAGE | CKA_ENCRYPT:
            flags = CKF_MESSAGE_ENCRYPT;
            break;
        case CKA_NSS_MESSAGE | CKA_DECRYPT:
            flags = CKF_MESSAGE_DECRYPT;
            break;
        case CKA_NSS_MESSAGE | CKA_SIGN:
            flags = CKF_MESSAGE_SIGN;
            break;
        case CKA_NSS_MESSAGE | CKA_VERIFY:
            flags = CKF_MESSAGE_VERIFY;
            break;
        default:
            break;
    }
    return flags;
}

/*
 * ******************** Hash Utilities **************************
 */
/*
 * Utility function for converting PSS/OAEP parameter types into
 * HASH_HashTypes. Note: Only SHA family functions are defined in RFC 3447.
 */
HASH_HashType
sftk_GetHashTypeFromMechanism(CK_MECHANISM_TYPE mech)
{
    switch (mech) {
        case CKM_SHA_1:
        case CKG_MGF1_SHA1:
            return HASH_AlgSHA1;
        case CKM_SHA224:
        case CKG_MGF1_SHA224:
            return HASH_AlgSHA224;
        case CKM_SHA256:
        case CKG_MGF1_SHA256:
            return HASH_AlgSHA256;
        case CKM_SHA384:
        case CKG_MGF1_SHA384:
            return HASH_AlgSHA384;
        case CKM_SHA512:
        case CKG_MGF1_SHA512:
            return HASH_AlgSHA512;
        default:
            return HASH_AlgNULL;
    }
}

#ifdef NSS_HAS_FIPS_INDICATORS
/**************** FIPS Indicator Utilities *************************/
/* sigh, we probably need a version of this in secutil so that both
 * softoken and NSS can use it */
static SECOidTag
sftk_quickGetECCCurveOid(SFTKObject *source)
{
    SFTKAttribute *attribute = sftk_FindAttribute(source, CKA_EC_PARAMS);
    unsigned char *encoded;
    int len;
    SECItem oid;
    SECOidTag tag;

    if (attribute == NULL) {
        return SEC_OID_UNKNOWN;
    }
    encoded = attribute->attrib.pValue;
    len = attribute->attrib.ulValueLen;
    if ((len < 2) || (encoded[0] != SEC_ASN1_OBJECT_ID) ||
        (len != encoded[1] + 2)) {
        sftk_FreeAttribute(attribute);
        return SEC_OID_UNKNOWN;
    }
    oid.data = encoded + 2;
    oid.len = len - 2;
    tag = SECOID_FindOIDTag(&oid);
    sftk_FreeAttribute(attribute);
    return tag;
}

/* This function currently only returns valid lengths for
 * FIPS approved ECC curves. If we want to make this generic
 * in the future, that Curve determination can be done in
 * the sftk_handleSpecial. Since it's currently only used
 * in FIPS indicators, it's currently only compiled with
 * the FIPS indicator code */
static CK_ULONG
sftk_getKeyLength(SFTKObject *source)
{
    CK_KEY_TYPE keyType = CKK_INVALID_KEY_TYPE;
    CK_ATTRIBUTE_TYPE keyAttribute;
    CK_ULONG keyLength = 0;
    SFTKAttribute *attribute;
    CK_RV crv;

    /* If we don't have a key, then it doesn't have a length.
     * this may be OK (say we are hashing). The mech info will
     * sort this out because algorithms which expect no keys
     * will accept zero length for the keys */
    if (source == NULL) {
        return 0;
    }

    crv = sftk_GetULongAttribute(source, CKA_KEY_TYPE, &keyType);
    if (crv != CKR_OK) {
        /* sometimes we're passed a data object, in that case the
         * key length is CKA_VALUE, which is the default */
        keyType = CKK_INVALID_KEY_TYPE;
    }
    if ((keyType == CKK_EC) || (keyType == CKK_EC_EDWARDS) ||
        (keyType == CKK_EC_MONTGOMERY)) {
        SECOidTag curve = sftk_quickGetECCCurveOid(source);
        switch (curve) {
            case SEC_OID_CURVE25519:
                /* change when we start algorithm testing on curve25519 */
                return 0;
            case SEC_OID_SECG_EC_SECP256R1:
                return 256;
            case SEC_OID_SECG_EC_SECP384R1:
                return 384;
            case SEC_OID_SECG_EC_SECP521R1:
                /* this is a lie, but it makes the table easier. We don't
                 * have to have a double entry for every ECC mechanism */
                return 512;
            default:
                break;
        }
        /* other curves aren't NIST approved, returning 0 will cause these
         * curves to fail FIPS length criteria */
        return 0;
    }

    switch (keyType) {
        case CKK_RSA:
            keyAttribute = CKA_MODULUS;
            break;
        case CKK_DSA:
        case CKK_DH:
            keyAttribute = CKA_PRIME;
            break;
        default:
            keyAttribute = CKA_VALUE;
            break;
    }
    attribute = sftk_FindAttribute(source, keyAttribute);
    if (attribute) {
        keyLength = attribute->attrib.ulValueLen * 8;
        sftk_FreeAttribute(attribute);
    }
    return keyLength;
}

static PRBool
sftk_checkKeyLength(CK_ULONG keyLength, CK_ULONG min,
                    CK_ULONG max, CK_ULONG step)
{
    if (keyLength > max) {
        return PR_FALSE;
    }
    if (keyLength < min) {
        return PR_FALSE;
    }
    if (step == 0) {
        return PR_TRUE;
    }
    if (((keyLength - min) % step) != 0) {
        return PR_FALSE;
    }
    return PR_TRUE;
}

PRBool
sftk_checkFIPSHash(CK_MECHANISM_TYPE hash, PRBool allowSmall, PRBool allowCMAC)
{
    switch (hash) {
        case CKM_AES_CMAC:
            return allowCMAC;
        case CKM_SHA_1:
        case CKM_SHA_1_HMAC:
        case CKM_SHA224:
        case CKM_SHA224_HMAC:
            return allowSmall;
        case CKM_SHA256:
        case CKM_SHA256_HMAC:
        case CKM_SHA384:
        case CKM_SHA384_HMAC:
        case CKM_SHA512:
        case CKM_SHA512_HMAC:
            return PR_TRUE;
    }
    return PR_FALSE;
}

/*
 * handle specialized FIPS semantics that are too complicated to
 * handle with just a table. NOTE: this means any additional semantics
 * would have to be coded here before they can be added to the table */
static PRBool
sftk_handleSpecial(SFTKSlot *slot, CK_MECHANISM *mech,
                   SFTKFIPSAlgorithmList *mechInfo, SFTKObject *source,
                   CK_ULONG keyLength, CK_ULONG targetKeyLength)
{
    PRBool allowSmall = PR_FALSE;
    PRBool allowCMAC = PR_FALSE;
    switch (mechInfo->special) {
        case SFTKFIPSDH: {
            SECItem dhPrime;
            SECItem dhBase;
            SECItem dhGenerator;
            PRBool fipsOk = PR_FALSE;
            const SECItem *dhSubPrime;
            CK_RV crv = sftk_Attribute2SecItem(NULL, &dhPrime,
                                               source, CKA_PRIME);
            if (crv != CKR_OK) {
                return PR_FALSE;
            }
            crv = sftk_Attribute2SecItem(NULL, &dhBase, source, CKA_BASE);
            if (crv != CKR_OK) {
                return PR_FALSE;
            }
            dhSubPrime = sftk_VerifyDH_Prime(&dhPrime, &dhGenerator, PR_TRUE);
            fipsOk = (dhSubPrime) ? PR_TRUE : PR_FALSE;
            if (fipsOk && (SECITEM_CompareItem(&dhBase, &dhGenerator) != 0)) {
                fipsOk = PR_FALSE;
            }

            SECITEM_ZfreeItem(&dhPrime, PR_FALSE);
            SECITEM_ZfreeItem(&dhBase, PR_FALSE);
            return fipsOk;
        }
        case SFTKFIPSNone:
            return PR_FALSE;
        case SFTKFIPSECC:
            /* we've already handled the curve selection in the 'getlength'
             * function */
            return PR_TRUE;
        case SFTKFIPSAEAD: {
            if (mech->ulParameterLen == 0) {
                /* AEAD ciphers are only in FIPS mode if we are using the
                 * MESSAGE interface. This takes an empty parameter
                 * in the init function */
                return PR_TRUE;
            }
            return PR_FALSE;
        }
        case SFTKFIPSRSAPSS: {
            /* PSS salt must not be longer than the  underlying hash.
             * We verify that the underlying hash of the
             * parameters matches Hash of the combined hash mechanisms, so
             * we don't need to look at the specific PSS mechanism */
            CK_RSA_PKCS_PSS_PARAMS *pss = (CK_RSA_PKCS_PSS_PARAMS *)
                                              mech->pParameter;
            const SECHashObject *hashObj = NULL;
            if (mech->ulParameterLen != sizeof(*pss)) {
                return PR_FALSE;
            }
            /* we use the existing hash utilities to find the length of
             * the hash */
            hashObj = HASH_GetRawHashObject(sftk_GetHashTypeFromMechanism(
                pss->hashAlg));
            if (hashObj == NULL) {
                return PR_FALSE;
            }
            /* cap the salt for legacy keys */
            if ((keyLength <= 1024) && (pss->sLen > 63)) {
                return PR_FALSE;
            }
            /* cap the salt based on the hash */
            if (pss->sLen > hashObj->length) {
                return PR_FALSE;
            }
            return PR_TRUE;
        }
        case SFTKFIPSPBKDF2: {
            /* from NIST SP 800-132
             * PBKDF2 must have the following addition restrictions
             * (independent of keysize).
             *    1. iteration count must be at least 1000.
             *    2. salt must be at least 128 bits (16 bytes).
             *    3. password must match the length specified in the SP
             */
            CK_PKCS5_PBKD2_PARAMS2 *pbkdf2 = (CK_PKCS5_PBKD2_PARAMS2 *)
                                                 mech->pParameter;
            if (mech->ulParameterLen != sizeof(*pbkdf2)) {
                return PR_FALSE;
            }
            if (pbkdf2 == NULL) {
                return PR_FALSE;
            }
            if (pbkdf2->iterations < 1000) {
                return PR_FALSE;
            }
            if (pbkdf2->ulSaltSourceDataLen < 16) {
                return PR_FALSE;
            }
            if (pbkdf2->ulPasswordLen < SFTKFIPS_PBKDF2_MIN_PW_LEN) {
                return PR_FALSE;
            }
            return PR_TRUE;
        }
        /* check the hash mechanisms to make sure they themselves are FIPS */
        case SFTKFIPSChkHashSp800:
            allowCMAC = PR_TRUE;
            /* fallthrough... will pick up allowSmall as well */
        case SFTKFIPSChkHash:
            allowSmall = PR_TRUE;
            /* fallthrough */
        case SFTKFIPSChkHashTls:
            if (mech->ulParameterLen < mechInfo->offset + sizeof(CK_ULONG)) {
                return PR_FALSE;
            }
            return sftk_checkFIPSHash(*(CK_MECHANISM_TYPE *)(((char *)mech->pParameter) + mechInfo->offset),
                                      allowSmall, allowCMAC);
        case SFTKFIPSTlsKeyCheck:
            if (mech->mechanism != CKM_NSS_TLS_KEY_AND_MAC_DERIVE_SHA256) {
                /* unless the mechnism has a built-in hash, check the hash */
                if (mech->ulParameterLen < mechInfo->offset + sizeof(CK_ULONG)) {
                    return PR_FALSE;
                }
                if (!sftk_checkFIPSHash(*(CK_MECHANISM_TYPE *)(((char *)mech->pParameter) + mechInfo->offset),
                                        PR_FALSE, PR_FALSE)) {
                    return PR_FALSE;
                }
            }
            return sftk_checkKeyLength(targetKeyLength, 112, 512, 1);
        case SFTKFIPSRSAOAEP: {
            CK_RSA_PKCS_OAEP_PARAMS *rsaoaep = (CK_RSA_PKCS_OAEP_PARAMS *)
                                                   mech->pParameter;

            HASH_HashType hash_msg = sftk_GetHashTypeFromMechanism(rsaoaep->hashAlg);
            HASH_HashType hash_pad = sftk_GetHashTypeFromMechanism(rsaoaep->mgf);
            /* message hash and mask generation function must be the same */
            if (hash_pad != hash_msg)
                return PR_FALSE;

            return sftk_checkFIPSHash(rsaoaep->hashAlg, PR_FALSE, PR_FALSE);
        }
        default:
            break;
    }
    /* if we didn't understand the special processing, mark it non-fips */
    return PR_FALSE;
}
#endif

PRBool
sftk_operationIsFIPS(SFTKSlot *slot, CK_MECHANISM *mech, CK_ATTRIBUTE_TYPE op,
                     SFTKObject *source, CK_ULONG targetKeyLength)
{
#ifndef NSS_HAS_FIPS_INDICATORS
    return PR_FALSE;
#else
    int i;
    CK_FLAGS opFlags;
    CK_ULONG keyLength;

    /* handle all the quick stuff first */
    if (!sftk_isFIPS(slot->slotID)) {
        return PR_FALSE;
    }
    if (source && !sftk_hasFIPS(source)) {
        return PR_FALSE;
    }
    if (mech == NULL) {
        return PR_FALSE;
    }

    /* now get the calculated values */
    opFlags = sftk_AttributeToFlags(op);
    if (opFlags == 0) {
        return PR_FALSE;
    }
    keyLength = sftk_getKeyLength(source);

    /* check against our algorithm array */
    for (i = 0; i < SFTK_NUMBER_FIPS_ALGORITHMS; i++) {
        SFTKFIPSAlgorithmList *mechs = &sftk_fips_mechs[i];
        /* if we match the number of records exactly, then we are an
         * approved algorithm in the approved mode with an approved key */
        if ((mech->mechanism == mechs->type) &&
            (opFlags == (mechs->info.flags & opFlags)) &&
            sftk_checkKeyLength(keyLength, mechs->info.ulMinKeySize,
                                mechs->info.ulMaxKeySize, mechs->step) &&
            ((targetKeyLength == 0) || (mechs->special == SFTKFIPSTlsKeyCheck) || sftk_checkKeyLength(targetKeyLength, mechs->info.ulMinKeySize, mechs->info.ulMaxKeySize, mechs->step)) &&
            ((mechs->special == SFTKFIPSNone) ||
             sftk_handleSpecial(slot, mech, mechs, source, keyLength, targetKeyLength))) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
#endif
}

void
sftk_setFIPS(SFTKObject *obj, PRBool isFIPS)
{
    if (isFIPS) {
        obj->validation_value |= SFTK_VALIDATION_FIPS_FLAG;
    } else {
        obj->validation_value &= ~SFTK_VALIDATION_FIPS_FLAG;
    }
}

PRBool
sftk_hasFIPS(SFTKObject *obj)
{
    return (obj->validation_value & SFTK_VALIDATION_FIPS_FLAG) ? PR_TRUE : PR_FALSE;
}

/*
 * create the FIPS Validation objects. If the vendor
 * doesn't supply an NSS_FIPS_MODULE_ID, at compile time,
 * then we assumethis is an unvalidated module.
 */
CK_RV
sftk_CreateValidationObjects(SFTKSlot *slot)
{
    const char *module_id;
    int module_id_len;
    CK_RV crv = CKR_OK;
    /* we currently use both vendor specific values and PKCS #11 v3.2
     * values for compatibility for a couple more ESR releases. Then we can
     * drop the vendor specific ones */
    CK_OBJECT_CLASS cko_nss_validation = CKO_NSS_VALIDATION;
    CK_OBJECT_CLASS cko_validation = CKO_VALIDATION;
    CK_NSS_VALIDATION_TYPE ckv_fips = CKV_NSS_FIPS_140;
    CK_FLAGS fipsFlag = SFTK_VALIDATION_FIPS_FLAG;
    CK_VALIDATION_TYPE swValidationType = CKV_TYPE_SOFTWARE;
    CK_VALIDATION_AUTHORITY_TYPE nistValidationAuthority =
        CKV_AUTHORITY_TYPE_NIST_CMVP;
    CK_UTF8CHAR us[] = { 'U', 'S' };
    CK_VERSION fips_version = { 3, 0 }; /* FIPS-140-3 */
    CK_ULONG fips_level = 1;            /* or 2 if you validated at level 2 */

#ifndef NSS_FIPS_MODULE_ID
#define NSS_FIPS_MODULE_ID "Generic NSS " SOFTOKEN_VERSION " Unvalidated"
#endif
    module_id = NSS_FIPS_MODULE_ID;
    module_id_len = sizeof(NSS_FIPS_MODULE_ID) - 1;
    SFTKObject *object;

    object = sftk_NewObject(slot); /* fill in the handle later */
    if (object == NULL) {
        return CKR_HOST_MEMORY;
    }
    sftk_setFIPS(object, PR_FALSE);

    crv = sftk_AddAttributeType(object, CKA_CLASS, &cko_nss_validation,
                                sizeof(cko_nss_validation));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_NSS_VALIDATION_TYPE,
                                &ckv_fips, sizeof(ckv_fips));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_NSS_VALIDATION_VERSION,
                                &fips_version, sizeof(fips_version));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_NSS_VALIDATION_LEVEL,
                                &fips_level, sizeof(fips_level));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_NSS_VALIDATION_MODULE_ID,
                                module_id, module_id_len);
    if (crv != CKR_OK) {
        goto loser;
    }

    object->handle = sftk_getNextHandle(slot);
    object->slot = slot;
    sftk_AddObject(&slot->moduleObjects, object);
    sftk_FreeObject(object);

    object = sftk_NewObject(slot); /* fill in the handle later */
    if (object == NULL) {
        return CKR_HOST_MEMORY;
    }
    sftk_setFIPS(object, PR_FALSE);
    crv = sftk_AddAttributeType(object, CKA_CLASS, &cko_validation,
                                sizeof(cko_validation));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_TYPE,
                                &swValidationType, sizeof(swValidationType));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_VERSION,
                                &fips_version, sizeof(fips_version));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_LEVEL,
                                &fips_level, sizeof(fips_level));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_MODULE_ID,
                                module_id, module_id_len);
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_FLAG,
                                &fipsFlag, sizeof(fipsFlag));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_AUTHORITY_TYPE,
                                &nistValidationAuthority,
                                sizeof(nistValidationAuthority));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_COUNTRY, us, sizeof(us));
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_CERTIFICATE_IDENTIFIER,
                                NULL, 0);
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_CERTIFICATE_URI,
                                NULL, 0);
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_PROFILE,
                                NULL, 0);
    if (crv != CKR_OK) {
        goto loser;
    }
    crv = sftk_AddAttributeType(object, CKA_VALIDATION_VENDOR_URI,
                                NULL, 0);
    if (crv != CKR_OK) {
        goto loser;
    }
    /* future, fill in validation certificate information from a supplied
     * pointer to a config file */
    object->handle = sftk_getNextHandle(slot);
    object->slot = slot;
    sftk_AddObject(&slot->moduleObjects, object);
loser:
    sftk_FreeObject(object);

    return crv;
}
