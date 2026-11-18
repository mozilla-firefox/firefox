/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Deal with PKCS #11 Slots.
 */

#include <stddef.h>

#include "seccomon.h"
#include "secmod.h"
#include "secmodi.h"
#include "secmodti.h"
#include "pkcs11t.h"
#include "pk11func.h"
#include "secitem.h"
#include "secerr.h"

#include "dev.h"
#include "dev3hack.h"
#include "pkim.h"
#include "utilpars.h"
#include "pkcs11uri.h"

/*************************************************************
 * local static and global data
 *************************************************************/

/*
 * This array helps parsing between names, mechanisms, and flags.
 * to make the config files understand more entries, add them
 * to this table.
 */
const PK11DefaultArrayEntry PK11_DefaultArray[] = {
    { "RSA", SECMOD_RSA_FLAG, CKM_RSA_PKCS },
    { "DSA", SECMOD_DSA_FLAG, CKM_DSA },
    { "ECC", SECMOD_ECC_FLAG, CKM_ECDSA },
    { "EDDSA", SECMOD_ECC_FLAG, CKM_EDDSA },
    { "DH", SECMOD_DH_FLAG, CKM_DH_PKCS_DERIVE },
    { "RC2", SECMOD_RC2_FLAG, CKM_RC2_CBC },
    { "RC4", SECMOD_RC4_FLAG, CKM_RC4 },
    { "DES", SECMOD_DES_FLAG, CKM_DES_CBC },
    { "AES", SECMOD_AES_FLAG, CKM_AES_CBC },
    { "Camellia", SECMOD_CAMELLIA_FLAG, CKM_CAMELLIA_CBC },
    { "SEED", SECMOD_SEED_FLAG, CKM_SEED_CBC },
    { "RC5", SECMOD_RC5_FLAG, CKM_RC5_CBC },
    { "SHA-1", SECMOD_SHA1_FLAG, CKM_SHA_1 },
    /*  { "SHA224", SECMOD_SHA256_FLAG, CKM_SHA224 }, */
    { "SHA256", SECMOD_SHA256_FLAG, CKM_SHA256 },
    /*  { "SHA384", SECMOD_SHA512_FLAG, CKM_SHA384 }, */
    { "SHA512", SECMOD_SHA512_FLAG, CKM_SHA512 },
    { "MD5", SECMOD_MD5_FLAG, CKM_MD5 },
    { "MD2", SECMOD_MD2_FLAG, CKM_MD2 },
    { "SSL", SECMOD_SSL_FLAG, CKM_SSL3_PRE_MASTER_KEY_GEN },
    { "TLS", SECMOD_TLS_FLAG, CKM_TLS_MASTER_KEY_DERIVE },
    { "SKIPJACK", SECMOD_FORTEZZA_FLAG, CKM_SKIPJACK_CBC64 },
    { "Publicly-readable certs", SECMOD_FRIENDLY_FLAG, CKM_INVALID_MECHANISM },
    { "Random Num Generator", SECMOD_RANDOM_FLAG, CKM_FAKE_RANDOM },
    { "ML-DSA", SECMOD_MLDSA_FLAG, CKM_ML_DSA },
};
const int num_pk11_default_mechanisms =
    sizeof(PK11_DefaultArray) / sizeof(PK11_DefaultArray[0]);

const PK11DefaultArrayEntry *
PK11_GetDefaultArray(int *size)
{
    if (size) {
        *size = num_pk11_default_mechanisms;
    }
    return PK11_DefaultArray;
}

/*
 * These  slotlists are lists of modules which provide default support for
 *  a given algorithm or mechanism.
 */
static PK11SlotList
    pk11_seedSlotList,
    pk11_camelliaSlotList,
    pk11_aesSlotList,
    pk11_desSlotList,
    pk11_rc4SlotList,
    pk11_rc2SlotList,
    pk11_rc5SlotList,
    pk11_sha1SlotList,
    pk11_md5SlotList,
    pk11_md2SlotList,
    pk11_rsaSlotList,
    pk11_dsaSlotList,
    pk11_dhSlotList,
    pk11_ecSlotList,
    pk11_ideaSlotList,
    pk11_sslSlotList,
    pk11_tlsSlotList,
    pk11_randomSlotList,
    pk11_sha256SlotList,
    pk11_sha512SlotList, /* slots do SHA512 and SHA384 */
    pk11_mldsaSlotList;

/************************************************************
 * Generic Slot List and Slot List element manipulations
 ************************************************************/

/*
 * allocate a new list
 */
PK11SlotList *
PK11_NewSlotList(void)
{
    PK11SlotList *list;

    list = (PK11SlotList *)PORT_Alloc(sizeof(PK11SlotList));
    if (list == NULL)
        return NULL;
    list->head = NULL;
    list->tail = NULL;
    list->lock = PR_NewLock();
    if (list->lock == NULL) {
        PORT_Free(list);
        return NULL;
    }

    return list;
}

/*
 * free a list element when all the references go away.
 */
SECStatus
PK11_FreeSlotListElement(PK11SlotList *list, PK11SlotListElement *le)
{
    PRBool freeit = PR_FALSE;

    if (list == NULL || le == NULL) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    PR_Lock(list->lock);
    PORT_ReleaseAssert(le->refCount > 0);
    if (le->refCount-- == 1) {
        freeit = PR_TRUE;
    }
    PR_Unlock(list->lock);
    if (freeit) {
        PK11_FreeSlot(le->slot);
        PORT_Free(le);
    }
    return SECSuccess;
}

static void
pk11_FreeSlotListStatic(PK11SlotList *list)
{
    PK11SlotListElement *le, *next;
    if (list == NULL)
        return;

    for (le = list->head; le; le = next) {
        next = le->next;
        PK11_FreeSlotListElement(list, le);
    }
    if (list->lock) {
        PR_DestroyLock(list->lock);
    }
    list->lock = NULL;
    list->head = NULL;
    list->tail = NULL;
}

/*
 * if we are freeing the list, we must be the only ones with a pointer
 * to the list.
 */
void
PK11_FreeSlotList(PK11SlotList *list)
{
    pk11_FreeSlotListStatic(list);
    PORT_Free(list);
}

/*
 * add a slot to a list
 * "slot" is the slot to be added. Ownership is not transferred.
 * "sorted" indicates whether or not the slot should be inserted according to
 *   cipherOrder of the associated module. PR_FALSE indicates that the slot
 *   should be inserted to the head of the list.
 */
SECStatus
PK11_AddSlotToList(PK11SlotList *list, PK11SlotInfo *slot, PRBool sorted)
{
    PK11SlotListElement *le;
    PK11SlotListElement *element;

    le = (PK11SlotListElement *)PORT_Alloc(sizeof(PK11SlotListElement));
    if (le == NULL)
        return SECFailure;

    le->slot = PK11_ReferenceSlot(slot);
    le->prev = NULL;
    le->refCount = 1;
    PR_Lock(list->lock);
    element = list->head;
    /* Insertion sort, with higher cipherOrders are sorted first in the list */
    while (element && sorted && (element->slot->module->cipherOrder > le->slot->module->cipherOrder)) {
        element = element->next;
    }
    if (element) {
        le->prev = element->prev;
        element->prev = le;
        le->next = element;
    } else {
        le->prev = list->tail;
        le->next = NULL;
        list->tail = le;
    }
    if (le->prev)
        le->prev->next = le;
    if (list->head == element)
        list->head = le;
    PR_Unlock(list->lock);

    return SECSuccess;
}

/*
 * remove a slot entry from the list
 */
SECStatus
PK11_DeleteSlotFromList(PK11SlotList *list, PK11SlotListElement *le)
{
    PR_Lock(list->lock);
    if (le->prev)
        le->prev->next = le->next;
    else
        list->head = le->next;
    if (le->next)
        le->next->prev = le->prev;
    else
        list->tail = le->prev;
    le->next = le->prev = NULL;
    PR_Unlock(list->lock);
    PK11_FreeSlotListElement(list, le);
    return SECSuccess;
}

/*
 * Move a list to the end of the target list.
 * NOTE: There is no locking here... This assumes BOTH lists are private copy
 * lists. It also does not re-sort the target list.
 */
SECStatus
pk11_MoveListToList(PK11SlotList *target, PK11SlotList *src)
{
    if (src->head == NULL)
        return SECSuccess;

    if (target->tail == NULL) {
        target->head = src->head;
    } else {
        target->tail->next = src->head;
    }
    src->head->prev = target->tail;
    target->tail = src->tail;
    src->head = src->tail = NULL;
    return SECSuccess;
}

/*
 * get an element safely from the list. This just makes sure that if
 * this element is not deleted while we deal with it.
 */
PK11SlotListElement *
PK11_GetFirstSafe(PK11SlotList *list)
{
    PK11SlotListElement *le;

    PR_Lock(list->lock);
    le = list->head;
    if (le != NULL)
        (le)->refCount++;
    PR_Unlock(list->lock);
    return le;
}

/*
 * NOTE: if this element gets deleted, we can no longer safely traverse using
 * it's pointers. We can either terminate the loop, or restart from the
 * beginning. This is controlled by the restart option.
 */
PK11SlotListElement *
PK11_GetNextSafe(PK11SlotList *list, PK11SlotListElement *le, PRBool restart)
{
    PK11SlotListElement *new_le;
    PR_Lock(list->lock);
    new_le = le->next;
    if (le->next == NULL) {
        /* if the prev and next fields are NULL then either this element
         * has been removed and we need to walk the list again (if restart
         * is true) or this was the only element on the list */
        if ((le->prev == NULL) && restart && (list->head != le)) {
            new_le = list->head;
        }
    }
    if (new_le)
        new_le->refCount++;
    PR_Unlock(list->lock);
    PK11_FreeSlotListElement(list, le);
    return new_le;
}

/*
 * Find the element that holds this slot
 */
PK11SlotListElement *
PK11_FindSlotElement(PK11SlotList *list, PK11SlotInfo *slot)
{
    PK11SlotListElement *le;

    for (le = PK11_GetFirstSafe(list); le;
         le = PK11_GetNextSafe(list, le, PR_TRUE)) {
        if (le->slot == slot)
            return le;
    }
    return NULL;
}

/* like PORT_Memcmp, return -1 if the version is less then the
 * passed in version, 0 if it's equal to and 1 if it's greater than
 * the passed in version, PKCS #11 returns versions in 2 places,
 * once in the function table and once in the module. the former
 * is good to determine if it is safe to call a new function,
 * the latter is good for module functionality */
PRInt32
PK11_CheckPKCS11Version(PK11SlotInfo *slot, CK_BYTE major, CK_BYTE minor,
                        PRBool useFunctionTable)
{
    CK_VERSION version = useFunctionTable ? PK11_GETTAB(slot)->version : slot->module->cryptokiVersion;

    if (version.major < major) {
        return -1;
    } else if (version.major > major) {
        return 1;
    } else if (version.minor < minor) {
        return -1;
    } else if (version.minor > minor) {
        return 1;
    }
    /* if we get here, they must both be equal */
    return 0;
}

/************************************************************
 * Generic Slot Utilities
 ************************************************************/
/*
 * Create a new slot structure
 */
PK11SlotInfo *
PK11_NewSlotInfo(SECMODModule *mod)
{
    PK11SlotInfo *slot;

    slot = (PK11SlotInfo *)PORT_Alloc(sizeof(PK11SlotInfo));
    if (slot == NULL) {
        return slot;
    }
    slot->freeListLock = PR_NewLock();
    if (slot->freeListLock == NULL) {
        PORT_Free(slot);
        return NULL;
    }
    slot->nssTokenLock = PR_NewLock();
    if (slot->nssTokenLock == NULL) {
        PR_DestroyLock(slot->freeListLock);
        PORT_Free(slot);
        return NULL;
    }
    slot->sessionLock = mod->isThreadSafe ? PR_NewLock() : mod->refLock;
    if (slot->sessionLock == NULL) {
        PR_DestroyLock(slot->nssTokenLock);
        PR_DestroyLock(slot->freeListLock);
        PORT_Free(slot);
        return NULL;
    }
    slot->freeSymKeysWithSessionHead = NULL;
    slot->freeSymKeysHead = NULL;
    slot->keyCount = 0;
    slot->maxKeyCount = 0;
    slot->functionList = NULL;
    slot->needTest = PR_TRUE;
    slot->isPerm = PR_FALSE;
    slot->isHW = PR_FALSE;
    slot->isInternal = PR_FALSE;
    slot->isThreadSafe = PR_FALSE;
    slot->disabled = PR_FALSE;
    slot->series = 1;
    slot->flagSeries = 0;
    slot->flagState = PR_FALSE;
    slot->wrapKey = 0;
    slot->wrapMechanism = CKM_INVALID_MECHANISM;
    slot->refKeys[0] = CK_INVALID_HANDLE;
    slot->reason = PK11_DIS_NONE;
    slot->readOnly = PR_TRUE;
    slot->needLogin = PR_FALSE;
    slot->hasRandom = PR_FALSE;
    slot->defRWSession = PR_FALSE;
    slot->protectedAuthPath = PR_FALSE;
    slot->flags = 0;
    slot->session = CK_INVALID_HANDLE;
    slot->slotID = 0;
    slot->defaultFlags = 0;
    slot->refCount = 1;
    slot->askpw = 0;
    slot->timeout = 0;
    slot->mechanismList = NULL;
    slot->mechanismCount = 0;
    slot->cert_array = NULL;
    slot->cert_count = 0;
    slot->slot_name[0] = 0;
    slot->token_name[0] = 0;
    PORT_Memset(slot->serial, ' ', sizeof(slot->serial));
    PORT_Memset(&slot->tokenInfo, 0, sizeof(slot->tokenInfo));
    slot->module = NULL;
    slot->authTime = LL_ZERO;
    slot->minPassword = 0;
    slot->maxPassword = 0;
    slot->hasRootCerts = PR_FALSE;
    slot->hasRootTrust = PR_FALSE;
    slot->nssToken = NULL;
    slot->profileList = NULL;
    slot->profileCount = 0;
    slot->validationFIPSFlags = 0;
    return slot;
}

/* create a new reference to a slot so it doesn't go away */
PK11SlotInfo *
PK11_ReferenceSlot(PK11SlotInfo *slot)
{
    PRInt32 refCount = PR_ATOMIC_INCREMENT(&slot->refCount);
    PORT_ReleaseAssert(refCount > 1);
    return slot;
}

/* Destroy all info on a slot we have built up */
void
PK11_DestroySlot(PK11SlotInfo *slot)
{
    /* free up the cached keys and sessions */
    PK11_CleanKeyList(slot);

    if (slot->functionList) {
        /* Destroy any cached wrapping keys (see PK11_SetWrapKey). */
        for (unsigned int i = 0; i < PR_ARRAY_SIZE(slot->refKeys); i++) {
            if (slot->refKeys[i] != CK_INVALID_HANDLE) {
                (void)PK11_DestroyObject(slot, slot->refKeys[i]);
                slot->refKeys[i] = CK_INVALID_HANDLE;
            }
        }

        /* free up all the sessions on this slot */
        PK11_GETTAB(slot)
            ->C_CloseAllSessions(slot->slotID);
    }

    if (slot->mechanismList) {
        PORT_Free(slot->mechanismList);
    }
    if (slot->profileList) {
        PORT_Free(slot->profileList);
    }
    if (slot->isThreadSafe && slot->sessionLock) {
        PR_DestroyLock(slot->sessionLock);
    }
    slot->sessionLock = NULL;
    if (slot->freeListLock) {
        PR_DestroyLock(slot->freeListLock);
        slot->freeListLock = NULL;
    }
    if (slot->nssTokenLock) {
        PR_DestroyLock(slot->nssTokenLock);
        slot->nssTokenLock = NULL;
    }

    /* finally Tell our parent module that we've gone away so it can unload */
    if (slot->module) {
        SECMOD_SlotDestroyModule(slot->module, PR_TRUE);
    }

    /* ok, well not quit finally... now we free the memory */
    PORT_Free(slot);
}

/* We're all done with the slot, free it */
void
PK11_FreeSlot(PK11SlotInfo *slot)
{
    PRInt32 refCount = PR_ATOMIC_DECREMENT(&slot->refCount);
    PORT_ReleaseAssert(refCount >= 0);
    if (refCount == 0) {
        PK11_DestroySlot(slot);
    }
}

void
PK11_EnterSlotMonitor(PK11SlotInfo *slot)
{
    PR_Lock(slot->sessionLock);
}

void
PK11_ExitSlotMonitor(PK11SlotInfo *slot)
{
    PR_Unlock(slot->sessionLock);
}

/***********************************************************
 * Functions to find specific slots.
 ***********************************************************/
PRBool
SECMOD_HasRootCerts(void)
{
    SECMODModuleList *mlp;
    SECMODModuleList *modules;
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();
    int i;
    PRBool found = PR_FALSE;

    if (!moduleLock) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return found;
    }

    /* work through all the slots */
    SECMOD_GetReadLock(moduleLock);
    modules = SECMOD_GetDefaultModuleList();
    for (mlp = modules; mlp != NULL; mlp = mlp->next) {
        for (i = 0; i < mlp->module->slotCount; i++) {
            PK11SlotInfo *tmpSlot = mlp->module->slots[i];
            if (PK11_IsPresent(tmpSlot)) {
                if (tmpSlot->hasRootCerts) {
                    found = PR_TRUE;
                    break;
                }
            }
        }
        if (found)
            break;
    }
    SECMOD_ReleaseReadLock(moduleLock);

    return found;
}

/***********************************************************
 * Functions to find specific slots.
 ***********************************************************/
PK11SlotList *
PK11_FindSlotsByNames(const char *dllName, const char *slotName,
                      const char *tokenName, PRBool presentOnly)
{
    SECMODModuleList *mlp;
    SECMODModuleList *modules;
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();
    int i;
    PK11SlotList *slotList = NULL;
    PRUint32 slotcount = 0;
    SECStatus rv = SECSuccess;

    if (!moduleLock) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return slotList;
    }

    slotList = PK11_NewSlotList();
    if (!slotList) {
        PORT_SetError(SEC_ERROR_NO_MEMORY);
        return slotList;
    }

    if (((NULL == dllName) || (0 == *dllName)) &&
        ((NULL == slotName) || (0 == *slotName)) &&
        ((NULL == tokenName) || (0 == *tokenName))) {
        /* default to softoken */
        /* PK11_GetInternalKeySlot increments the refcount on the internal slot,
         * but so does PK11_AddSlotToList. To avoid erroneously increasing the
         * refcount twice, we get our own reference to the internal slot and
         * decrement its refcount when we're done with it. */
        PK11SlotInfo *internalKeySlot = PK11_GetInternalKeySlot();
        PK11_AddSlotToList(slotList, internalKeySlot, PR_TRUE);
        PK11_FreeSlot(internalKeySlot);
        return slotList;
    }

    /* work through all the slots */
    SECMOD_GetReadLock(moduleLock);
    modules = SECMOD_GetDefaultModuleList();
    for (mlp = modules; mlp != NULL; mlp = mlp->next) {
        PORT_Assert(mlp->module);
        if (!mlp->module) {
            rv = SECFailure;
            break;
        }
        if ((!dllName) || (mlp->module->dllName &&
                           (0 == PORT_Strcmp(mlp->module->dllName, dllName)))) {
            for (i = 0; i < mlp->module->slotCount; i++) {
                PK11SlotInfo *tmpSlot = (mlp->module->slots ? mlp->module->slots[i] : NULL);
                PORT_Assert(tmpSlot);
                if (!tmpSlot) {
                    rv = SECFailure;
                    break;
                }
                if ((PR_FALSE == presentOnly || PK11_IsPresent(tmpSlot)) &&
                    ((!tokenName) ||
                     (0 == PORT_Strcmp(tmpSlot->token_name, tokenName))) &&
                    ((!slotName) ||
                     (0 == PORT_Strcmp(tmpSlot->slot_name, slotName)))) {
                    PK11_AddSlotToList(slotList, tmpSlot, PR_TRUE);
                    slotcount++;
                }
            }
        }
    }
    SECMOD_ReleaseReadLock(moduleLock);

    if ((0 == slotcount) || (SECFailure == rv)) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        PK11_FreeSlotList(slotList);
        slotList = NULL;
    }

    if (SECFailure == rv) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    }

    return slotList;
}

typedef PRBool (*PK11SlotMatchFunc)(PK11SlotInfo *slot, const void *arg);

static PRBool
pk11_MatchSlotByTokenName(PK11SlotInfo *slot, const void *arg)
{
    return PORT_Strcmp(slot->token_name, arg) == 0;
}

static PRBool
pk11_MatchSlotByTokenURI(PK11SlotInfo *slot, const void *arg)
{
    return pk11_MatchUriTokenInfo(slot, (PK11URI *)arg);
}

static PK11SlotInfo *
pk11_FindSlot(const void *arg, PK11SlotMatchFunc func)
{
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();
    SECMODModuleList *mlp;
    SECMODModuleList *modules;
    int i;
    PK11SlotInfo *slot = NULL;

    if (!moduleLock) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return slot;
    }
    /* work through all the slots */
    SECMOD_GetReadLock(moduleLock);
    modules = SECMOD_GetDefaultModuleList();
    for (mlp = modules; mlp != NULL; mlp = mlp->next) {
        for (i = 0; i < mlp->module->slotCount; i++) {
            PK11SlotInfo *tmpSlot = mlp->module->slots[i];
            if (PK11_IsPresent(tmpSlot)) {
                if (func(tmpSlot, arg)) {
                    slot = PK11_ReferenceSlot(tmpSlot);
                    break;
                }
            }
        }
        if (slot != NULL)
            break;
    }
    SECMOD_ReleaseReadLock(moduleLock);

    if (slot == NULL) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
    }

    return slot;
}

static PK11SlotInfo *
pk11_FindSlotByTokenURI(const char *uriString)
{
    PK11SlotInfo *slot = NULL;
    PK11URI *uri;

    uri = PK11URI_ParseURI(uriString);
    if (!uri) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return slot;
    }

    slot = pk11_FindSlot(uri, pk11_MatchSlotByTokenURI);
    PK11URI_DestroyURI(uri);
    return slot;
}

PK11SlotInfo *
PK11_FindSlotByName(const char *name)
{
    if ((name == NULL) || (*name == 0)) {
        return PK11_GetInternalKeySlot();
    }

    if (!PORT_Strncasecmp(name, "pkcs11:", strlen("pkcs11:"))) {
        return pk11_FindSlotByTokenURI(name);
    }

    return pk11_FindSlot(name, pk11_MatchSlotByTokenName);
}

/*
 * notification stub. If we ever get interested in any events that
 * the pkcs11 functions may pass back to use, we can catch them here...
 * currently pdata is a slotinfo structure.
 */
CK_RV
pk11_notify(CK_SESSION_HANDLE session, CK_NOTIFICATION event,
            CK_VOID_PTR pdata)
{
    return CKR_OK;
}

/*
 * grab a new RW session
 * !!! has a side effect of grabbing the Monitor if either the slot's default
 * session is RW or the slot is not thread safe. Monitor is release in function
 * below
 */
CK_SESSION_HANDLE
PK11_GetRWSession(PK11SlotInfo *slot)
{
    CK_SESSION_HANDLE rwsession;
    CK_RV crv;
    PRBool haveMonitor = PR_FALSE;

    if (!slot->isThreadSafe || slot->defRWSession) {
        PK11_EnterSlotMonitor(slot);
        haveMonitor = PR_TRUE;
    }
    if (slot->defRWSession) {
        PORT_Assert(slot->session != CK_INVALID_HANDLE);
        if (slot->session != CK_INVALID_HANDLE)
            return slot->session;
    }

    crv = PK11_GETTAB(slot)->C_OpenSession(slot->slotID,
                                           CKF_RW_SESSION | CKF_SERIAL_SESSION,
                                           slot, pk11_notify, &rwsession);
    PORT_Assert(rwsession != CK_INVALID_HANDLE || crv != CKR_OK);
    if (crv != CKR_OK || rwsession == CK_INVALID_HANDLE) {
        if (crv == CKR_OK)
            crv = CKR_DEVICE_ERROR;
        if (haveMonitor)
            PK11_ExitSlotMonitor(slot);
        PORT_SetError(PK11_MapError(crv));
        return CK_INVALID_HANDLE;
    }
    if (slot->defRWSession) { /* we have the monitor */
        slot->session = rwsession;
    }
    return rwsession;
}

PRBool
PK11_RWSessionHasLock(PK11SlotInfo *slot, CK_SESSION_HANDLE session_handle)
{
    PRBool hasLock;
    hasLock = (PRBool)(!slot->isThreadSafe ||
                       (slot->defRWSession && slot->session != CK_INVALID_HANDLE));
    return hasLock;
}

static PRBool
pk11_RWSessionIsDefault(PK11SlotInfo *slot, CK_SESSION_HANDLE rwsession)
{
    PRBool isDefault;
    isDefault = (PRBool)(slot->session == rwsession &&
                         slot->defRWSession &&
                         slot->session != CK_INVALID_HANDLE);
    return isDefault;
}

/*
 * close the rwsession and restore our readonly session
 * !!! has a side effect of releasing the Monitor if either the slot's default
 * session is RW or the slot is not thread safe.
 */
void
PK11_RestoreROSession(PK11SlotInfo *slot, CK_SESSION_HANDLE rwsession)
{
    PORT_Assert(rwsession != CK_INVALID_HANDLE);
    if (rwsession != CK_INVALID_HANDLE) {
        PRBool doExit = PK11_RWSessionHasLock(slot, rwsession);
        if (!pk11_RWSessionIsDefault(slot, rwsession))
            PK11_GETTAB(slot)
                ->C_CloseSession(rwsession);
        if (doExit)
            PK11_ExitSlotMonitor(slot);
    }
}

/************************************************************
 * Manage the built-In Slot Lists
 ************************************************************/

/* Init the static built int slot list (should actually integrate
 * with PK11_NewSlotList */
static void
pk11_InitSlotListStatic(PK11SlotList *list)
{
    list->lock = PR_NewLock();
    list->head = NULL;
    list->tail = NULL;
}

/* initialize the system slotlists */
SECStatus
PK11_InitSlotLists(void)
{
    pk11_InitSlotListStatic(&pk11_seedSlotList);
    pk11_InitSlotListStatic(&pk11_camelliaSlotList);
    pk11_InitSlotListStatic(&pk11_aesSlotList);
    pk11_InitSlotListStatic(&pk11_desSlotList);
    pk11_InitSlotListStatic(&pk11_rc4SlotList);
    pk11_InitSlotListStatic(&pk11_rc2SlotList);
    pk11_InitSlotListStatic(&pk11_rc5SlotList);
    pk11_InitSlotListStatic(&pk11_md5SlotList);
    pk11_InitSlotListStatic(&pk11_md2SlotList);
    pk11_InitSlotListStatic(&pk11_sha1SlotList);
    pk11_InitSlotListStatic(&pk11_rsaSlotList);
    pk11_InitSlotListStatic(&pk11_dsaSlotList);
    pk11_InitSlotListStatic(&pk11_dhSlotList);
    pk11_InitSlotListStatic(&pk11_ecSlotList);
    pk11_InitSlotListStatic(&pk11_ideaSlotList);
    pk11_InitSlotListStatic(&pk11_sslSlotList);
    pk11_InitSlotListStatic(&pk11_tlsSlotList);
    pk11_InitSlotListStatic(&pk11_randomSlotList);
    pk11_InitSlotListStatic(&pk11_sha256SlotList);
    pk11_InitSlotListStatic(&pk11_sha512SlotList);
    pk11_InitSlotListStatic(&pk11_mldsaSlotList);
    return SECSuccess;
}

void
PK11_DestroySlotLists(void)
{
    pk11_FreeSlotListStatic(&pk11_seedSlotList);
    pk11_FreeSlotListStatic(&pk11_camelliaSlotList);
    pk11_FreeSlotListStatic(&pk11_aesSlotList);
    pk11_FreeSlotListStatic(&pk11_desSlotList);
    pk11_FreeSlotListStatic(&pk11_rc4SlotList);
    pk11_FreeSlotListStatic(&pk11_rc2SlotList);
    pk11_FreeSlotListStatic(&pk11_rc5SlotList);
    pk11_FreeSlotListStatic(&pk11_md5SlotList);
    pk11_FreeSlotListStatic(&pk11_md2SlotList);
    pk11_FreeSlotListStatic(&pk11_sha1SlotList);
    pk11_FreeSlotListStatic(&pk11_rsaSlotList);
    pk11_FreeSlotListStatic(&pk11_dsaSlotList);
    pk11_FreeSlotListStatic(&pk11_dhSlotList);
    pk11_FreeSlotListStatic(&pk11_ecSlotList);
    pk11_FreeSlotListStatic(&pk11_ideaSlotList);
    pk11_FreeSlotListStatic(&pk11_sslSlotList);
    pk11_FreeSlotListStatic(&pk11_tlsSlotList);
    pk11_FreeSlotListStatic(&pk11_randomSlotList);
    pk11_FreeSlotListStatic(&pk11_sha256SlotList);
    pk11_FreeSlotListStatic(&pk11_sha512SlotList);
    pk11_FreeSlotListStatic(&pk11_mldsaSlotList);
    return;
}

/* return a system slot list based on mechanism */
PK11SlotList *
PK11_GetSlotList(CK_MECHANISM_TYPE type)
{
/* XXX a workaround for Bugzilla bug #55267 */
#if defined(HPUX) && defined(__LP64__)
    if (CKM_INVALID_MECHANISM == type)
        return NULL;
#endif
    switch (type) {
        case CKM_SEED_CBC:
        case CKM_SEED_ECB:
            return &pk11_seedSlotList;
        case CKM_CAMELLIA_CBC:
        case CKM_CAMELLIA_ECB:
            return &pk11_camelliaSlotList;
        case CKM_AES_CBC:
        case CKM_AES_CCM:
        case CKM_AES_CTR:
        case CKM_AES_CTS:
        case CKM_AES_GCM:
        case CKM_AES_ECB:
            return &pk11_aesSlotList;
        case CKM_DES_CBC:
        case CKM_DES_ECB:
        case CKM_DES3_ECB:
        case CKM_DES3_CBC:
            return &pk11_desSlotList;
        case CKM_RC4:
            return &pk11_rc4SlotList;
        case CKM_RC5_CBC:
            return &pk11_rc5SlotList;
        case CKM_SHA_1:
            return &pk11_sha1SlotList;
        case CKM_SHA224:
        case CKM_SHA256:
        case CKM_SHA3_224:
        case CKM_SHA3_256:
            return &pk11_sha256SlotList;
        case CKM_SHA384:
        case CKM_SHA512:
        case CKM_SHA3_384:
        case CKM_SHA3_512:
            return &pk11_sha512SlotList;
        case CKM_MD5:
            return &pk11_md5SlotList;
        case CKM_MD2:
            return &pk11_md2SlotList;
        case CKM_RC2_ECB:
        case CKM_RC2_CBC:
            return &pk11_rc2SlotList;
        case CKM_RSA_PKCS:
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
        case CKM_RSA_X_509:
            return &pk11_rsaSlotList;
        case CKM_DSA:
            return &pk11_dsaSlotList;
        case CKM_DH_PKCS_KEY_PAIR_GEN:
        case CKM_DH_PKCS_DERIVE:
            return &pk11_dhSlotList;
        case CKM_EDDSA:
        case CKM_EC_EDWARDS_KEY_PAIR_GEN:
        case CKM_ECDSA:
        case CKM_ECDSA_SHA1:
        case CKM_EC_KEY_PAIR_GEN: /* aka CKM_ECDSA_KEY_PAIR_GEN */
        case CKM_NSS_ECDHE_NO_PAIRWISE_CHECK_KEY_PAIR_GEN:
        case CKM_ECDH1_DERIVE:
        case CKM_NSS_KYBER_KEY_PAIR_GEN: /* Bug 1893029 */
        case CKM_NSS_KYBER:
        case CKM_NSS_ML_KEM_KEY_PAIR_GEN: /* Bug 1893029 */
        case CKM_NSS_ML_KEM:
        case CKM_ML_KEM_KEY_PAIR_GEN: /* Bug 1893029 */
        case CKM_ML_KEM:
            return &pk11_ecSlotList;
        case CKM_SSL3_PRE_MASTER_KEY_GEN:
        case CKM_SSL3_MASTER_KEY_DERIVE:
        case CKM_SSL3_SHA1_MAC:
        case CKM_SSL3_MD5_MAC:
            return &pk11_sslSlotList;
        case CKM_TLS_MASTER_KEY_DERIVE:
        case CKM_TLS_KEY_AND_MAC_DERIVE:
        case CKM_NSS_TLS_KEY_AND_MAC_DERIVE_SHA256:
            return &pk11_tlsSlotList;
        case CKM_IDEA_CBC:
        case CKM_IDEA_ECB:
            return &pk11_ideaSlotList;
        case CKM_FAKE_RANDOM:
            return &pk11_randomSlotList;
        case CKM_ML_DSA:
            return &pk11_mldsaSlotList;
    }
    return NULL;
}

/*
 * load the static SlotInfo structures used to select a PKCS11 slot.
 * preSlotInfo has a list of all the default flags for the slots on this
 * module.
 */
void
PK11_LoadSlotList(PK11SlotInfo *slot, PK11PreSlotInfo *psi, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (psi[i].slotID == slot->slotID)
            break;
    }

    if (i == count)
        return;

    slot->defaultFlags = psi[i].defaultFlags;
    slot->askpw = psi[i].askpw;
    slot->timeout = psi[i].timeout;
    slot->hasRootCerts = psi[i].hasRootCerts;

    /* if the slot is already disabled, don't load them into the
     * default slot lists. We get here so we can save the default
     * list value. */
    if (slot->disabled)
        return;

    /* if the user has disabled us, don't load us in */
    if (slot->defaultFlags & PK11_DISABLE_FLAG) {
        slot->disabled = PR_TRUE;
        slot->reason = PK11_DIS_USER_SELECTED;
        /* free up sessions and things?? */
        return;
    }

    for (i = 0; i < num_pk11_default_mechanisms; i++) {
        if (slot->defaultFlags & PK11_DefaultArray[i].flag) {
            CK_MECHANISM_TYPE mechanism = PK11_DefaultArray[i].mechanism;
            PK11SlotList *slotList = PK11_GetSlotList(mechanism);

            if (slotList)
                PK11_AddSlotToList(slotList, slot, PR_FALSE);
        }
    }

    return;
}

/*
 * update a slot to its new attribute according to the slot list
 * returns: SECSuccess if nothing to do or add/delete is successful
 */
SECStatus
PK11_UpdateSlotAttribute(PK11SlotInfo *slot,
                         const PK11DefaultArrayEntry *entry,
                         PRBool add)
/* add: PR_TRUE if want to turn on */
{
    SECStatus result = SECSuccess;
    PK11SlotList *slotList = PK11_GetSlotList(entry->mechanism);

    if (add) { /* trying to turn on a mechanism */

        /* turn on the default flag in the slot */
        slot->defaultFlags |= entry->flag;

        /* add this slot to the list */
        if (slotList != NULL)
            result = PK11_AddSlotToList(slotList, slot, PR_FALSE);

    } else { /* trying to turn off */

        /* turn OFF the flag in the slot */
        slot->defaultFlags &= ~entry->flag;

        if (slotList) {
            /* find the element in the list & delete it */
            PK11SlotListElement *le = PK11_FindSlotElement(slotList, slot);

            /* remove the slot from the list */
            if (le)
                result = PK11_DeleteSlotFromList(slotList, le);
        }
    }
    return result;
}

/*
 * clear a slot off of all of it's default list
 */
void
PK11_ClearSlotList(PK11SlotInfo *slot)
{
    int i;

    if (slot->disabled)
        return;
    if (slot->defaultFlags == 0)
        return;

    for (i = 0; i < num_pk11_default_mechanisms; i++) {
        if (slot->defaultFlags & PK11_DefaultArray[i].flag) {
            CK_MECHANISM_TYPE mechanism = PK11_DefaultArray[i].mechanism;
            PK11SlotList *slotList = PK11_GetSlotList(mechanism);
            PK11SlotListElement *le = NULL;

            if (slotList)
                le = PK11_FindSlotElement(slotList, slot);

            if (le) {
                PK11_DeleteSlotFromList(slotList, le);
                PK11_FreeSlotListElement(slotList, le);
            }
        }
    }
}

/******************************************************************
 *           Slot initialization
 ******************************************************************/
/*
 * turn a PKCS11 Static Label into a string
 */
char *
PK11_MakeString(PLArenaPool *arena, char *space,
                char *staticString, int stringLen)
{
    int i;
    char *newString;
    for (i = (stringLen - 1); i >= 0; i--) {
        if (staticString[i] != ' ')
            break;
    }
    /* move i to point to the last space */
    i++;
    if (arena) {
        newString = (char *)PORT_ArenaAlloc(arena, i + 1 /* space for NULL */);
    } else if (space) {
        newString = space;
    } else {
        newString = (char *)PORT_Alloc(i + 1 /* space for NULL */);
    }
    if (newString == NULL)
        return NULL;

    if (i)
        PORT_Memcpy(newString, staticString, i);
    newString[i] = 0;

    return newString;
}

/*
 * check if a null-terminated string matches with a PKCS11 Static Label
 */
PRBool
pk11_MatchString(const char *string,
                 const char *staticString, size_t staticStringLen)
{
    size_t i = staticStringLen;

    /* move i to point to the last space */
    while (i > 0) {
        if (staticString[i - 1] != ' ')
            break;
        i--;
    }

    if (strlen(string) == i && memcmp(string, staticString, i) == 0) {
        return PR_TRUE;
    }

    return PR_FALSE;
}

/*
 * Reads in the slots mechanism list for later use
 */
SECStatus
PK11_ReadMechanismList(PK11SlotInfo *slot)
{
    CK_ULONG count;
    CK_RV crv;
    PRUint32 i;

    if (slot->mechanismList) {
        PORT_Free(slot->mechanismList);
        slot->mechanismList = NULL;
    }
    slot->mechanismCount = 0;

    if (!slot->isThreadSafe)
        PK11_EnterSlotMonitor(slot);
    crv = PK11_GETTAB(slot)->C_GetMechanismList(slot->slotID, NULL, &count);
    if (crv != CKR_OK) {
        if (!slot->isThreadSafe)
            PK11_ExitSlotMonitor(slot);
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }

    slot->mechanismList = (CK_MECHANISM_TYPE *)
        PORT_Alloc(count * sizeof(CK_MECHANISM_TYPE));
    if (slot->mechanismList == NULL) {
        if (!slot->isThreadSafe)
            PK11_ExitSlotMonitor(slot);
        return SECFailure;
    }
    crv = PK11_GETTAB(slot)->C_GetMechanismList(slot->slotID,
                                                slot->mechanismList, &count);
    if (!slot->isThreadSafe)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_Free(slot->mechanismList);
        slot->mechanismList = NULL;
        PORT_SetError(PK11_MapError(crv));
        return SECSuccess;
    }
    slot->mechanismCount = count;
    PORT_Memset(slot->mechanismBits, 0, sizeof(slot->mechanismBits));

    for (i = 0; i < count; i++) {
        CK_MECHANISM_TYPE mech = slot->mechanismList[i];
        if (mech < 0x7ff) {
            slot->mechanismBits[mech & 0xff] |= 1 << (mech >> 8);
        }
    }
    return SECSuccess;
}

static SECStatus
pk11_ReadProfileList(PK11SlotInfo *slot)
{
    CK_ATTRIBUTE findTemp[2];
    CK_ATTRIBUTE *attrs;
    CK_BBOOL cktrue = CK_TRUE;
    CK_OBJECT_CLASS oclass = CKO_PROFILE;
    size_t tsize;
    int objCount;
    CK_OBJECT_HANDLE *handles = NULL;
    int i;

    attrs = findTemp;
    PK11_SETATTRS(attrs, CKA_TOKEN, &cktrue, sizeof(cktrue));
    attrs++;
    PK11_SETATTRS(attrs, CKA_CLASS, &oclass, sizeof(oclass));
    attrs++;
    tsize = attrs - findTemp;
    PORT_Assert(tsize <= sizeof(findTemp) / sizeof(CK_ATTRIBUTE));

    if (slot->profileList) {
        PORT_Free(slot->profileList);
        slot->profileList = NULL;
    }
    slot->profileCount = 0;

    objCount = 0;
    handles = pk11_FindObjectsByTemplate(slot, findTemp, tsize, &objCount);
    if (handles == NULL) {
        if (objCount < 0) {
            return SECFailure; /* error code is set */
        }
        PORT_Assert(objCount == 0);
        return SECSuccess;
    }

    slot->profileList = (CK_PROFILE_ID *)
        PORT_Alloc(objCount * sizeof(CK_PROFILE_ID));
    if (slot->profileList == NULL) {
        PORT_Free(handles);
        return SECFailure; /* error code is set */
    }

    for (i = 0; i < objCount; i++) {
        CK_ULONG value;

        value = PK11_ReadULongAttribute(slot, handles[i], CKA_PROFILE_ID);
        if (value == CK_UNAVAILABLE_INFORMATION) {
            continue;
        }
        slot->profileList[slot->profileCount++] = value;
    }

    PORT_Free(handles);
    return SECSuccess;
}

static PRBool
pk11_HasProfile(PK11SlotInfo *slot, CK_PROFILE_ID id)
{
    int i;

    for (i = 0; i < slot->profileCount; i++) {
        if (slot->profileList[i] == id) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

static CK_FLAGS
pk11_GetValidationFlags(PK11SlotInfo *slot, CK_VALIDATION_AUTHORITY_TYPE auth)
{
    CK_ATTRIBUTE findTemp[2];
    CK_ATTRIBUTE *attrs;
    CK_OBJECT_CLASS oclass = CKO_VALIDATION;
    size_t tsize;
    int objCount;
    CK_OBJECT_HANDLE *handles = NULL;
    CK_FLAGS validation_flags = 0;
    int i;

    /* only used with tokens with verison >= 3.2 */
    if (PK11_CheckPKCS11Version(slot, 3, 2, PR_FALSE) < 0) {
        return validation_flags;
    }

    attrs = findTemp;
    PK11_SETATTRS(attrs, CKA_CLASS, &oclass, sizeof(oclass));
    attrs++;
    PK11_SETATTRS(attrs, CKA_VALIDATION_AUTHORITY_TYPE, &auth, sizeof(auth));
    attrs++;
    tsize = attrs - findTemp;
    PORT_Assert(tsize <= sizeof(findTemp) / sizeof(CK_ATTRIBUTE));

    objCount = 0;
    handles = pk11_FindObjectsByTemplate(slot, findTemp, tsize, &objCount);
    if (handles == NULL) {
        /* none found, return or empty flags */
        return validation_flags;
    }

    for (i = 0; i < objCount; i++) {
        CK_FLAGS value;
        value = PK11_ReadULongAttribute(slot, handles[i], CKA_VALIDATION_FLAG);
        if (value == CK_UNAVAILABLE_INFORMATION) {
            continue;
        }
        validation_flags |= value;
    }

    PORT_Free(handles);
    return validation_flags;
}

/*
 * initialize a new token
 * unlike initialize slot, this can be called multiple times in the lifetime
 * of NSS. It reads the information associated with a card or token,
 * that is not going to change unless the card or token changes.
 */
SECStatus
PK11_InitToken(PK11SlotInfo *slot, PRBool loadCerts)
{
    CK_RV crv;
    SECStatus rv;
    PRStatus status;
    NSSToken *nssToken;

    /* set the slot flags to the current token values */
    if (!slot->isThreadSafe)
        PK11_EnterSlotMonitor(slot);
    crv = PK11_GETTAB(slot)->C_GetTokenInfo(slot->slotID, &slot->tokenInfo);
    if (!slot->isThreadSafe)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }

    /* set the slot flags to the current token values */
    slot->series++; /* allow other objects to detect that the
                     * slot is different */
    slot->flags = slot->tokenInfo.flags;
    slot->needLogin = ((slot->tokenInfo.flags & CKF_LOGIN_REQUIRED) ? PR_TRUE : PR_FALSE);
    slot->readOnly = ((slot->tokenInfo.flags & CKF_WRITE_PROTECTED) ? PR_TRUE : PR_FALSE);

    slot->hasRandom = ((slot->tokenInfo.flags & CKF_RNG) ? PR_TRUE : PR_FALSE);
    slot->protectedAuthPath =
        ((slot->tokenInfo.flags & CKF_PROTECTED_AUTHENTICATION_PATH)
             ? PR_TRUE
             : PR_FALSE);
    pk11_SetLastLoginCheck(slot, 0);
    slot->lastState = 0;
    /* on some platforms Active Card incorrectly sets the
     * CKF_PROTECTED_AUTHENTICATION_PATH bit when it doesn't mean to. */
    if (slot->isActiveCard) {
        slot->protectedAuthPath = PR_FALSE;
    }
    (void)PK11_MakeString(NULL, slot->token_name,
                          (char *)slot->tokenInfo.label, sizeof(slot->tokenInfo.label));
    slot->minPassword = slot->tokenInfo.ulMinPinLen;
    slot->maxPassword = slot->tokenInfo.ulMaxPinLen;
    PORT_Memcpy(slot->serial, slot->tokenInfo.serialNumber, sizeof(slot->serial));

    nssToken = PK11Slot_GetNSSToken(slot);
    nssToken_UpdateName(nssToken); /* null token is OK */
    (void)nssToken_Destroy(nssToken);

    slot->defRWSession = (PRBool)((!slot->readOnly) &&
                                  (slot->tokenInfo.ulMaxSessionCount == 1));
    rv = PK11_ReadMechanismList(slot);
    if (rv != SECSuccess)
        return rv;

    slot->hasRSAInfo = PR_FALSE;
    slot->RSAInfoFlags = 0;

    /* initialize the maxKeyCount value */
    PR_Lock(slot->freeListLock);
    if (slot->tokenInfo.ulMaxSessionCount == 0) {
        slot->maxKeyCount = 800; /* should be #define or a config param */
    } else if (slot->tokenInfo.ulMaxSessionCount < 20) {
        /* don't have enough sessions to keep that many keys around */
        slot->maxKeyCount = 0;
    } else {
        slot->maxKeyCount = slot->tokenInfo.ulMaxSessionCount / 2;
    }
    PR_Unlock(slot->freeListLock);

    /* Make sure our session handle is valid */
    if (slot->session == CK_INVALID_HANDLE) {
        /* we know we don't have a valid session, go get one */
        CK_SESSION_HANDLE session;

        /* session should be Readonly, serial */
        if (!slot->isThreadSafe)
            PK11_EnterSlotMonitor(slot);
        crv = PK11_GETTAB(slot)->C_OpenSession(slot->slotID,
                                               (slot->defRWSession ? CKF_RW_SESSION : 0) | CKF_SERIAL_SESSION,
                                               slot, pk11_notify, &session);
        if (!slot->isThreadSafe)
            PK11_ExitSlotMonitor(slot);
        if (crv != CKR_OK) {
            PORT_SetError(PK11_MapError(crv));
            return SECFailure;
        }
        slot->session = session;
    } else {
        /* The session we have may be defunct (the token associated with it)
         * has been removed   */
        CK_SESSION_INFO sessionInfo;

        if (!slot->isThreadSafe)
            PK11_EnterSlotMonitor(slot);
        crv = PK11_GETTAB(slot)->C_GetSessionInfo(slot->session, &sessionInfo);
        if (crv == CKR_DEVICE_ERROR) {
            PK11_GETTAB(slot)
                ->C_CloseSession(slot->session);
            crv = CKR_SESSION_CLOSED;
        }
        if ((crv == CKR_SESSION_CLOSED) || (crv == CKR_SESSION_HANDLE_INVALID)) {
            crv = PK11_GETTAB(slot)->C_OpenSession(slot->slotID,
                                                   (slot->defRWSession ? CKF_RW_SESSION : 0) | CKF_SERIAL_SESSION,
                                                   slot, pk11_notify, &slot->session);
            if (crv != CKR_OK) {
                PORT_SetError(PK11_MapError(crv));
                slot->session = CK_INVALID_HANDLE;
                if (!slot->isThreadSafe)
                    PK11_ExitSlotMonitor(slot);
                return SECFailure;
            }
        }
        if (!slot->isThreadSafe)
            PK11_ExitSlotMonitor(slot);
    }

    nssToken = PK11Slot_GetNSSToken(slot);
    status = nssToken_Refresh(nssToken); /* null token is OK */
    (void)nssToken_Destroy(nssToken);
    if (status != PR_SUCCESS)
        return SECFailure;

    /* Not all tokens have profile objects or even recognize what profile
     * objects are it's OK for pk11_ReadProfileList to fail */
    (void)pk11_ReadProfileList(slot);
    slot->validationFIPSFlags =
        pk11_GetValidationFlags(slot, CKV_AUTHORITY_TYPE_NIST_CMVP);

    if (!(slot->isInternal) && (slot->hasRandom)) {
        /* if this slot has a random number generater, use it to add entropy
         * to the internal slot. */
        PK11SlotInfo *int_slot = PK11_GetInternalSlot();

        if (int_slot) {
            unsigned char random_bytes[32];

            /* if this slot can issue random numbers, get some entropy from
             * that random number generater and give it to our internal token.
             */
            PK11_EnterSlotMonitor(slot);
            crv = PK11_GETTAB(slot)->C_GenerateRandom(slot->session, random_bytes, sizeof(random_bytes));
            PK11_ExitSlotMonitor(slot);
            if (crv == CKR_OK) {
                PK11_EnterSlotMonitor(int_slot);
                PK11_GETTAB(int_slot)
                    ->C_SeedRandom(int_slot->session,
                                   random_bytes, sizeof(random_bytes));
                PK11_ExitSlotMonitor(int_slot);
            }

            /* Now return the favor and send entropy to the token's random
             * number generater */
            PK11_EnterSlotMonitor(int_slot);
            crv = PK11_GETTAB(int_slot)->C_GenerateRandom(int_slot->session,
                                                          random_bytes, sizeof(random_bytes));
            PK11_ExitSlotMonitor(int_slot);
            if (crv == CKR_OK) {
                PK11_EnterSlotMonitor(slot);
                crv = PK11_GETTAB(slot)->C_SeedRandom(slot->session,
                                                      random_bytes, sizeof(random_bytes));
                PK11_ExitSlotMonitor(slot);
            }
            PK11_FreeSlot(int_slot);
        }
    }

    return SECSuccess;
}

/*
 * initialize a new token
 * unlike initialize slot, this can be called multiple times in the lifetime
 * of NSS. It reads the information associated with a card or token,
 * that is not going to change unless the card or token changes.
 */
SECStatus
PK11_TokenRefresh(PK11SlotInfo *slot)
{
    CK_RV crv;

    /* set the slot flags to the current token values */
    if (!slot->isThreadSafe)
        PK11_EnterSlotMonitor(slot);
    crv = PK11_GETTAB(slot)->C_GetTokenInfo(slot->slotID, &slot->tokenInfo);
    if (!slot->isThreadSafe)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }

    slot->flags = slot->tokenInfo.flags;
    slot->needLogin = ((slot->tokenInfo.flags & CKF_LOGIN_REQUIRED) ? PR_TRUE : PR_FALSE);
    slot->readOnly = ((slot->tokenInfo.flags & CKF_WRITE_PROTECTED) ? PR_TRUE : PR_FALSE);
    slot->hasRandom = ((slot->tokenInfo.flags & CKF_RNG) ? PR_TRUE : PR_FALSE);
    slot->protectedAuthPath =
        ((slot->tokenInfo.flags & CKF_PROTECTED_AUTHENTICATION_PATH)
             ? PR_TRUE
             : PR_FALSE);
    /* on some platforms Active Card incorrectly sets the
     * CKF_PROTECTED_AUTHENTICATION_PATH bit when it doesn't mean to. */
    if (slot->isActiveCard) {
        slot->protectedAuthPath = PR_FALSE;
    }
    return SECSuccess;
}

static PRBool
pk11_isRootSlot(PK11SlotInfo *slot)
{
    CK_ATTRIBUTE findTemp[1];
    CK_ATTRIBUTE *attrs;
    CK_OBJECT_CLASS oclass = CKO_NSS_BUILTIN_ROOT_LIST;
    size_t tsize;
    CK_OBJECT_HANDLE handle;

    attrs = findTemp;
    PK11_SETATTRS(attrs, CKA_CLASS, &oclass, sizeof(oclass));
    attrs++;
    tsize = attrs - findTemp;
    PORT_Assert(tsize <= sizeof(findTemp) / sizeof(CK_ATTRIBUTE));

    handle = pk11_FindObjectByTemplate(slot, findTemp, tsize);
    if (handle == CK_INVALID_HANDLE) {
        return PR_FALSE;
    }
    return PR_TRUE;
}

/*
 * Initialize the slot :
 * This initialization code is called on each slot a module supports when
 * it is loaded. It does the bringup initialization. The difference between
 * this and InitToken is Init slot does those one time initialization stuff,
 * usually associated with the reader, while InitToken may get called multiple
 * times as tokens are removed and re-inserted.
 */
void
PK11_InitSlot(SECMODModule *mod, CK_SLOT_ID slotID, PK11SlotInfo *slot)
{
    SECStatus rv;
    CK_SLOT_INFO slotInfo;

    slot->functionList = mod->functionList;
    slot->isInternal = mod->internal;
    slot->slotID = slotID;
    slot->isThreadSafe = mod->isThreadSafe;
    slot->hasRSAInfo = PR_FALSE;
    slot->module = mod; /* NOTE: we don't make a reference here because
                         * modules have references to their slots. This
                         * works because modules keep implicit references
                         * from their slots, and won't unload and disappear
                         * until all their slots have been freed */

    if (PK11_GetSlotInfo(slot, &slotInfo) != SECSuccess) {
        slot->disabled = PR_TRUE;
        slot->reason = PK11_DIS_COULD_NOT_INIT_TOKEN;
        return;
    }

    /* test to make sure claimed mechanism work */
    slot->needTest = mod->internal ? PR_FALSE : PR_TRUE;
    (void)PK11_MakeString(NULL, slot->slot_name,
                          (char *)slotInfo.slotDescription, sizeof(slotInfo.slotDescription));
    slot->isHW = (PRBool)((slotInfo.flags & CKF_HW_SLOT) == CKF_HW_SLOT);
#define ACTIVE_CARD "ActivCard SA"
    slot->isActiveCard = (PRBool)(PORT_Strncmp((char *)slotInfo.manufacturerID,
                                               ACTIVE_CARD, sizeof(ACTIVE_CARD) - 1) == 0);
    if ((slotInfo.flags & CKF_REMOVABLE_DEVICE) == 0) {
        slot->isPerm = PR_TRUE;
        /* permanment slots must have the token present always */
        if ((slotInfo.flags & CKF_TOKEN_PRESENT) == 0) {
            slot->disabled = PR_TRUE;
            slot->reason = PK11_DIS_TOKEN_NOT_PRESENT;
            return; /* nothing else to do */
        }
    }
    /* if the token is present, initialize it */
    if ((slotInfo.flags & CKF_TOKEN_PRESENT) != 0) {
        rv = PK11_InitToken(slot, PR_TRUE);
        /* the only hard failures are on permanent devices, or function
         * verify failures... function verify failures are already handled
         * by tokenInit */
        if ((rv != SECSuccess) && (slot->isPerm) && (!slot->disabled)) {
            slot->disabled = PR_TRUE;
            slot->reason = PK11_DIS_COULD_NOT_INIT_TOKEN;
        }
        if (rv == SECSuccess && pk11_isRootSlot(slot)) {
            if (!slot->hasRootCerts) {
                slot->module->trustOrder = 100;
            }
            slot->hasRootCerts = PR_TRUE;
        }
    }
}

/*********************************************************************
 *            Slot mapping utility functions.
 *********************************************************************/

/*
 * determine if the token is present. If the token is present, make sure
 * we have a valid session handle. Also set the value of needLogin
 * appropriately.
 */
static PRBool
pk11_IsPresentCertLoad(PK11SlotInfo *slot, PRBool loadCerts)
{
    CK_SLOT_INFO slotInfo;
    CK_SESSION_INFO sessionInfo;
    CK_RV crv;

    /* disabled slots are never present */
    if (slot->disabled) {
        return PR_FALSE;
    }

    /* permanent slots are always present */
    if (slot->isPerm && (slot->session != CK_INVALID_HANDLE)) {
        return PR_TRUE;
    }

    NSSToken *nssToken = PK11Slot_GetNSSToken(slot);
    if (nssToken) {
        PRBool present = nssToken_IsPresent(nssToken);
        (void)nssToken_Destroy(nssToken);
        return present;
    }

    /* removable slots have a flag that says they are present */
    if (PK11_GetSlotInfo(slot, &slotInfo) != SECSuccess) {
        return PR_FALSE;
    }

    if ((slotInfo.flags & CKF_TOKEN_PRESENT) == 0) {
        /* if the slot is no longer present, close the session */
        if (slot->session != CK_INVALID_HANDLE) {
            if (!slot->isThreadSafe) {
                PK11_EnterSlotMonitor(slot);
            }
            PK11_GETTAB(slot)
                ->C_CloseSession(slot->session);
            slot->session = CK_INVALID_HANDLE;
            if (!slot->isThreadSafe) {
                PK11_ExitSlotMonitor(slot);
            }
        }
        return PR_FALSE;
    }

    /* use the session Info to determine if the card has been removed and then
     * re-inserted */
    if (slot->session != CK_INVALID_HANDLE) {
        if (slot->isThreadSafe) {
            PK11_EnterSlotMonitor(slot);
        }
        crv = PK11_GETTAB(slot)->C_GetSessionInfo(slot->session, &sessionInfo);
        if (crv != CKR_OK) {
            PK11_GETTAB(slot)
                ->C_CloseSession(slot->session);
            slot->session = CK_INVALID_HANDLE;
        }
        if (slot->isThreadSafe) {
            PK11_ExitSlotMonitor(slot);
        }
    }

    /* card has not been removed, current token info is correct */
    if (slot->session != CK_INVALID_HANDLE)
        return PR_TRUE;

    /* initialize the token info state */
    if (PK11_InitToken(slot, loadCerts) != SECSuccess) {
        return PR_FALSE;
    }

    return PR_TRUE;
}

/*
 * old version of the routine
 */
PRBool
PK11_IsPresent(PK11SlotInfo *slot)
{
    return pk11_IsPresentCertLoad(slot, PR_TRUE);
}

/* is the slot disabled? */
PRBool
PK11_IsDisabled(PK11SlotInfo *slot)
{
    return slot->disabled;
}

/* and why? */
PK11DisableReasons
PK11_GetDisabledReason(PK11SlotInfo *slot)
{
    return slot->reason;
}

/* returns PR_TRUE if successfully disable the slot */
/* returns PR_FALSE otherwise */
PRBool
PK11_UserDisableSlot(PK11SlotInfo *slot)
{

    /* Prevent users from disabling the internal module. */
    if (slot->isInternal) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return PR_FALSE;
    }

    slot->defaultFlags |= PK11_DISABLE_FLAG;
    slot->disabled = PR_TRUE;
    slot->reason = PK11_DIS_USER_SELECTED;

    return PR_TRUE;
}

PRBool
PK11_UserEnableSlot(PK11SlotInfo *slot)
{

    slot->defaultFlags &= ~PK11_DISABLE_FLAG;
    slot->disabled = PR_FALSE;
    slot->reason = PK11_DIS_NONE;
    return PR_TRUE;
}

PRBool
PK11_HasRootCerts(PK11SlotInfo *slot)
{
    return slot->hasRootCerts;
}

/* Get the module this slot is attached to */
SECMODModule *
PK11_GetModule(PK11SlotInfo *slot)
{
    return slot->module;
}

/* return the default flags of a slot */
unsigned long
PK11_GetDefaultFlags(PK11SlotInfo *slot)
{
    return slot->defaultFlags;
}

/*
 * The following wrapper functions allow us to export an opaque slot
 * function to the rest of libsec and the world... */
PRBool
PK11_IsReadOnly(PK11SlotInfo *slot)
{
    return slot->readOnly;
}

PRBool
PK11_IsHW(PK11SlotInfo *slot)
{
    return slot->isHW;
}

PRBool
PK11_IsRemovable(PK11SlotInfo *slot)
{
    return !slot->isPerm;
}

PRBool
PK11_IsInternal(PK11SlotInfo *slot)
{
    return slot->isInternal;
}

PRBool
PK11_IsInternalKeySlot(PK11SlotInfo *slot)
{
    PK11SlotInfo *int_slot;
    PRBool result;

    if (!slot->isInternal) {
        return PR_FALSE;
    }

    int_slot = PK11_GetInternalKeySlot();
    result = (int_slot == slot) ? PR_TRUE : PR_FALSE;
    PK11_FreeSlot(int_slot);
    return result;
}

PRBool
PK11_NeedLogin(PK11SlotInfo *slot)
{
    return slot->needLogin;
}

PRBool
PK11_IsFriendly(PK11SlotInfo *slot)
{
    /* internal slot always has public readable certs */
    return (PRBool)(slot->isInternal ||
                    pk11_HasProfile(slot, CKP_PUBLIC_CERTIFICATES_TOKEN) ||
                    ((slot->defaultFlags & SECMOD_FRIENDLY_FLAG) ==
                     SECMOD_FRIENDLY_FLAG));
}

char *
PK11_GetTokenName(PK11SlotInfo *slot)
{
    return slot->token_name;
}

char *
PK11_GetTokenURI(PK11SlotInfo *slot)
{
    PK11URI *uri;
    char *ret = NULL;
    char label[32 + 1], manufacturer[32 + 1], serial[16 + 1], model[16 + 1];
    PK11URIAttribute attrs[4];
    size_t nattrs = 0;

    PK11_MakeString(NULL, label, (char *)slot->tokenInfo.label,
                    sizeof(slot->tokenInfo.label));
    if (*label != '\0') {
        attrs[nattrs].name = PK11URI_PATTR_TOKEN;
        attrs[nattrs].value = label;
        nattrs++;
    }

    PK11_MakeString(NULL, manufacturer, (char *)slot->tokenInfo.manufacturerID,
                    sizeof(slot->tokenInfo.manufacturerID));
    if (*manufacturer != '\0') {
        attrs[nattrs].name = PK11URI_PATTR_MANUFACTURER;
        attrs[nattrs].value = manufacturer;
        nattrs++;
    }

    PK11_MakeString(NULL, serial, (char *)slot->tokenInfo.serialNumber,
                    sizeof(slot->tokenInfo.serialNumber));
    if (*serial != '\0') {
        attrs[nattrs].name = PK11URI_PATTR_SERIAL;
        attrs[nattrs].value = serial;
        nattrs++;
    }

    PK11_MakeString(NULL, model, (char *)slot->tokenInfo.model,
                    sizeof(slot->tokenInfo.model));
    if (*model != '\0') {
        attrs[nattrs].name = PK11URI_PATTR_MODEL;
        attrs[nattrs].value = model;
        nattrs++;
    }

    uri = PK11URI_CreateURI(attrs, nattrs, NULL, 0);
    if (uri == NULL) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
        return NULL;
    }

    ret = PK11URI_FormatURI(NULL, uri);
    PK11URI_DestroyURI(uri);

    if (ret == NULL) {
        PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
    }

    return ret;
}

char *
PK11_GetSlotName(PK11SlotInfo *slot)
{
    return slot->slot_name;
}

int
PK11_GetSlotSeries(PK11SlotInfo *slot)
{
    return slot->series;
}

int
PK11_GetCurrentWrapIndex(PK11SlotInfo *slot)
{
    return slot->wrapKey;
}

CK_SLOT_ID
PK11_GetSlotID(PK11SlotInfo *slot)
{
    return slot->slotID;
}

SECMODModuleID
PK11_GetModuleID(PK11SlotInfo *slot)
{
    return slot->module->moduleID;
}

static void
pk11_zeroTerminatedToBlankPadded(CK_CHAR *buffer, size_t buffer_size)
{
    CK_CHAR *walk = buffer;
    CK_CHAR *end = buffer + buffer_size;

    /* find the NULL */
    while (walk < end && *walk != '\0') {
        walk++;
    }

    /* clear out the buffer */
    while (walk < end) {
        *walk++ = ' ';
    }
}

/* return the slot info structure */
SECStatus
PK11_GetSlotInfo(PK11SlotInfo *slot, CK_SLOT_INFO *info)
{
    CK_RV crv;

    if (!slot->isThreadSafe)
        PK11_EnterSlotMonitor(slot);
    /*
     * some buggy drivers do not fill the buffer completely,
     * erase the buffer first
     */
    PORT_Memset(info->slotDescription, ' ', sizeof(info->slotDescription));
    PORT_Memset(info->manufacturerID, ' ', sizeof(info->manufacturerID));
    crv = PK11_GETTAB(slot)->C_GetSlotInfo(slot->slotID, info);
    pk11_zeroTerminatedToBlankPadded(info->slotDescription,
                                     sizeof(info->slotDescription));
    pk11_zeroTerminatedToBlankPadded(info->manufacturerID,
                                     sizeof(info->manufacturerID));
    if (!slot->isThreadSafe)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }
    return SECSuccess;
}

/*  return the token info structure */
SECStatus
PK11_GetTokenInfo(PK11SlotInfo *slot, CK_TOKEN_INFO *info)
{
    CK_RV crv;
    if (!slot->isThreadSafe)
        PK11_EnterSlotMonitor(slot);
    /*
     * some buggy drivers do not fill the buffer completely,
     * erase the buffer first
     */
    PORT_Memset(info->label, ' ', sizeof(info->label));
    PORT_Memset(info->manufacturerID, ' ', sizeof(info->manufacturerID));
    PORT_Memset(info->model, ' ', sizeof(info->model));
    PORT_Memset(info->serialNumber, ' ', sizeof(info->serialNumber));
    crv = PK11_GETTAB(slot)->C_GetTokenInfo(slot->slotID, info);
    pk11_zeroTerminatedToBlankPadded(info->label, sizeof(info->label));
    pk11_zeroTerminatedToBlankPadded(info->manufacturerID,
                                     sizeof(info->manufacturerID));
    pk11_zeroTerminatedToBlankPadded(info->model, sizeof(info->model));
    pk11_zeroTerminatedToBlankPadded(info->serialNumber,
                                     sizeof(info->serialNumber));
    if (!slot->isThreadSafe)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }
    return SECSuccess;
}

PRBool
pk11_MatchUriTokenInfo(PK11SlotInfo *slot, PK11URI *uri)
{
    const char *value;

    value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_TOKEN);
    if (value) {
        if (!pk11_MatchString(value, (char *)slot->tokenInfo.label,
                              sizeof(slot->tokenInfo.label))) {
            return PR_FALSE;
        }
    }

    value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_MANUFACTURER);
    if (value) {
        if (!pk11_MatchString(value, (char *)slot->tokenInfo.manufacturerID,
                              sizeof(slot->tokenInfo.manufacturerID))) {
            return PR_FALSE;
        }
    }

    value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_SERIAL);
    if (value) {
        if (!pk11_MatchString(value, (char *)slot->tokenInfo.serialNumber,
                              sizeof(slot->tokenInfo.serialNumber))) {
            return PR_FALSE;
        }
    }

    value = PK11URI_GetPathAttribute(uri, PK11URI_PATTR_MODEL);
    if (value) {
        if (!pk11_MatchString(value, (char *)slot->tokenInfo.model,
                              sizeof(slot->tokenInfo.model))) {
            return PR_FALSE;
        }
    }

    return PR_TRUE;
}

/* Find out if we need to initialize the user's pin */
PRBool
PK11_NeedUserInit(PK11SlotInfo *slot)
{
    PRBool needUserInit = (PRBool)((slot->flags & CKF_USER_PIN_INITIALIZED) == 0);

    if (needUserInit) {
        CK_TOKEN_INFO info;
        SECStatus rv;

        /* see if token has been initialized off line */
        rv = PK11_GetTokenInfo(slot, &info);
        if (rv == SECSuccess) {
            slot->flags = info.flags;
        }
    }
    return (PRBool)((slot->flags & CKF_USER_PIN_INITIALIZED) == 0);
}

static PK11SlotInfo *pk11InternalKeySlot = NULL;

/*
 * Set a new default internal keyslot. If one has already been set, clear it.
 * Passing NULL falls back to the NSS normally selected default internal key
 * slot.
 */
void
pk11_SetInternalKeySlot(PK11SlotInfo *slot)
{
    if (pk11InternalKeySlot) {
        PK11_FreeSlot(pk11InternalKeySlot);
    }
    pk11InternalKeySlot = slot ? PK11_ReferenceSlot(slot) : NULL;
}

/*
 * Set a new default internal keyslot if the normal key slot has not already
 * been overridden. Subsequent calls to this function will be ignored unless
 * pk11_SetInternalKeySlot is used to clear the current default.
 */
void
pk11_SetInternalKeySlotIfFirst(PK11SlotInfo *slot)
{
    if (pk11InternalKeySlot) {
        return;
    }
    pk11InternalKeySlot = slot ? PK11_ReferenceSlot(slot) : NULL;
}

/*
 * Swap out a default internal keyslot.  Caller owns the Slot Reference
 */
PK11SlotInfo *
pk11_SwapInternalKeySlot(PK11SlotInfo *slot)
{
    PK11SlotInfo *swap = pk11InternalKeySlot;

    pk11InternalKeySlot = slot ? PK11_ReferenceSlot(slot) : NULL;
    return swap;
}

/* get the internal key slot. FIPS has only one slot for both key slots and
 * default slots */
PK11SlotInfo *
PK11_GetInternalKeySlot(void)
{
    SECMODModule *mod;

    if (pk11InternalKeySlot) {
        return PK11_ReferenceSlot(pk11InternalKeySlot);
    }

    mod = SECMOD_GetInternalModule();
    PORT_Assert(mod != NULL);
    if (!mod) {
        PORT_SetError(SEC_ERROR_NO_MODULE);
        return NULL;
    }
    return PK11_ReferenceSlot(mod->isFIPS ? mod->slots[0] : mod->slots[1]);
}

/* get the internal default slot */
PK11SlotInfo *
PK11_GetInternalSlot(void)
{
    SECMODModule *mod = SECMOD_GetInternalModule();
    PORT_Assert(mod != NULL);
    if (!mod) {
        PORT_SetError(SEC_ERROR_NO_MODULE);
        return NULL;
    }
    if (mod->isFIPS) {
        return PK11_GetInternalKeySlot();
    }
    return PK11_ReferenceSlot(mod->slots[0]);
}

/*
 * check if a given slot supports the requested mechanism
 */
PRBool
PK11_DoesMechanism(PK11SlotInfo *slot, CK_MECHANISM_TYPE type)
{
    int i;

    /* CKM_FAKE_RANDOM is not a real PKCS mechanism. It's a marker to
     * tell us we're looking form someone that has implemented get
     * random bits */
    if (type == CKM_FAKE_RANDOM) {
        return slot->hasRandom;
    }

    /* for most mechanism, bypass the linear lookup */
    if (type < 0x7ff) {
        return (slot->mechanismBits[type & 0xff] & (1 << (type >> 8))) ? PR_TRUE : PR_FALSE;
    }

    for (i = 0; i < (int)slot->mechanismCount; i++) {
        if (slot->mechanismList[i] == type)
            return PR_TRUE;
    }
    return PR_FALSE;
}

PRBool pk11_filterSlot(PK11SlotInfo *slot, CK_MECHANISM_TYPE mechanism,
                       CK_FLAGS mechanismInfoFlags, unsigned int keySize);
/*
 * Check that the given mechanism has the appropriate flags. This function
 * presumes that slot can already do the given mechanism.
 */
PRBool
PK11_DoesMechanismFlag(PK11SlotInfo *slot, CK_MECHANISM_TYPE type,
                       CK_FLAGS flags)
{
    return !pk11_filterSlot(slot, type, flags, 0);
}

/*
 * Return true if a token that can do the desired mechanism exists.
 * This allows us to have hardware tokens that can do function XYZ magically
 * allow SSL Ciphers to appear if they are plugged in.
 */
PRBool
PK11_TokenExists(CK_MECHANISM_TYPE type)
{
    SECMODModuleList *mlp;
    SECMODModuleList *modules;
    SECMODListLock *moduleLock = SECMOD_GetDefaultModuleListLock();
    PK11SlotInfo *slot;
    PRBool found = PR_FALSE;
    int i;

    if (!moduleLock) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return found;
    }
    /* we only need to know if there is a token that does this mechanism.
     * check the internal module first because it's fast, and supports
     * almost everything. */
    slot = PK11_GetInternalSlot();
    if (slot) {
        found = PK11_DoesMechanism(slot, type);
        PK11_FreeSlot(slot);
    }
    if (found)
        return PR_TRUE; /* bypass getting module locks */

    SECMOD_GetReadLock(moduleLock);
    modules = SECMOD_GetDefaultModuleList();
    for (mlp = modules; mlp != NULL && (!found); mlp = mlp->next) {
        for (i = 0; i < mlp->module->slotCount; i++) {
            slot = mlp->module->slots[i];
            if (PK11_IsPresent(slot)) {
                if (PK11_DoesMechanism(slot, type)) {
                    found = PR_TRUE;
                    break;
                }
            }
        }
    }
    SECMOD_ReleaseReadLock(moduleLock);
    return found;
}

/*
 * get all the currently available tokens in a list.
 * that can perform the given mechanism. If mechanism is CKM_INVALID_MECHANISM,
 * get all the tokens. Make sure tokens that need authentication are put at
 * the end of this list.
 */
PK11SlotList *
PK11_GetAllTokens(CK_MECHANISM_TYPE type, PRBool needRW, PRBool loadCerts,
                  void *wincx)
{
    PK11SlotList *list;
    PK11SlotList *loginList;
    PK11SlotList *friendlyList;
    SECMODModuleList *mlp;
    SECMODModuleList *modules;
    SECMODListLock *moduleLock;
    int i;

    moduleLock = SECMOD_GetDefaultModuleListLock();
    if (!moduleLock) {
        PORT_SetError(SEC_ERROR_NOT_INITIALIZED);
        return NULL;
    }

    list = PK11_NewSlotList();
    loginList = PK11_NewSlotList();
    friendlyList = PK11_NewSlotList();
    if ((list == NULL) || (loginList == NULL) || (friendlyList == NULL)) {
        if (list)
            PK11_FreeSlotList(list);
        if (loginList)
            PK11_FreeSlotList(loginList);
        if (friendlyList)
            PK11_FreeSlotList(friendlyList);
        return NULL;
    }

    SECMOD_GetReadLock(moduleLock);

    modules = SECMOD_GetDefaultModuleList();
    for (mlp = modules; mlp != NULL; mlp = mlp->next) {
        for (i = 0; i < mlp->module->slotCount; i++) {
            PK11SlotInfo *slot = mlp->module->slots[i];

            if (pk11_IsPresentCertLoad(slot, loadCerts)) {
                if (needRW && slot->readOnly)
                    continue;
                if ((type == CKM_INVALID_MECHANISM) || PK11_DoesMechanism(slot, type)) {
                    if (pk11_LoginStillRequired(slot, wincx)) {
                        if (PK11_IsFriendly(slot)) {
                            PK11_AddSlotToList(friendlyList, slot, PR_TRUE);
                        } else {
                            PK11_AddSlotToList(loginList, slot, PR_TRUE);
                        }
                    } else {
                        PK11_AddSlotToList(list, slot, PR_TRUE);
                    }
                }
            }
        }
    }
    SECMOD_ReleaseReadLock(moduleLock);

    pk11_MoveListToList(list, friendlyList);
    PK11_FreeSlotList(friendlyList);
    pk11_MoveListToList(list, loginList);
    PK11_FreeSlotList(loginList);

    return list;
}

/*
 * returns true if the slot doesn't conform to the requested attributes
 */
PRBool
pk11_filterSlot(PK11SlotInfo *slot, CK_MECHANISM_TYPE mechanism,
                CK_FLAGS mechanismInfoFlags, unsigned int keySize)
{
    CK_MECHANISM_INFO mechanism_info;
    CK_RV crv = CKR_OK;

    /* handle the only case where we don't actually fetch the mechanisms
     * on the fly */
    if ((keySize == 0) && (mechanism == CKM_RSA_PKCS) && (slot->hasRSAInfo)) {
        mechanism_info.flags = slot->RSAInfoFlags;
    } else {
        if (!slot->isThreadSafe)
            PK11_EnterSlotMonitor(slot);
        crv = PK11_GETTAB(slot)->C_GetMechanismInfo(slot->slotID, mechanism,
                                                    &mechanism_info);
        if (!slot->isThreadSafe)
            PK11_ExitSlotMonitor(slot);
        /* if we were getting the RSA flags, save them */
        if ((crv == CKR_OK) && (mechanism == CKM_RSA_PKCS) && (!slot->hasRSAInfo)) {
            slot->RSAInfoFlags = mechanism_info.flags;
            slot->hasRSAInfo = PR_TRUE;
        }
    }
    /* couldn't get the mechanism info */
    if (crv != CKR_OK) {
        return PR_TRUE;
    }
    if (keySize && ((mechanism_info.ulMinKeySize > keySize) || (mechanism_info.ulMaxKeySize < keySize))) {
        /* Token can do mechanism, but not at the key size we
         * want */
        return PR_TRUE;
    }
    if (mechanismInfoFlags && ((mechanism_info.flags & mechanismInfoFlags) !=
                               mechanismInfoFlags)) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

/*
 * Find the best slot which supports the given set of mechanisms and key sizes.
 * In normal cases this should grab the first slot on the list with no fuss.
 * The size array is presumed to match one for one with the mechanism type
 * array, which allows you to specify the required key size for each
 * mechanism in the list. Whether key size is in bits or bytes is mechanism
 * dependent. Typically asymetric keys are in bits and symetric keys are in
 * bytes.
 */
PK11SlotInfo *
PK11_GetBestSlotMultipleWithAttributes(CK_MECHANISM_TYPE *type,
                                       CK_FLAGS *mechanismInfoFlags, unsigned int *keySize,
                                       unsigned int mech_count, void *wincx)
{
    PK11SlotList *list = NULL;
    PK11SlotListElement *le;
    PK11SlotInfo *slot = NULL;
    PRBool freeit = PR_FALSE;
    PRBool listNeedLogin = PR_FALSE;
    unsigned int i;
    SECStatus rv;

    list = PK11_GetSlotList(type[0]);

    if ((list == NULL) || (list->head == NULL)) {
        /* We need to look up all the tokens for the mechanism */
        list = PK11_GetAllTokens(type[0], PR_FALSE, PR_TRUE, wincx);
        freeit = PR_TRUE;
    }

    /* no one can do it! */
    if (list == NULL) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
        return NULL;
    }

    PORT_SetError(0);

    listNeedLogin = PR_FALSE;
    for (i = 0; i < mech_count; i++) {
        if ((type[i] != CKM_FAKE_RANDOM) &&
            (type[i] != CKM_SHA_1) &&
            (type[i] != CKM_SHA224) &&
            (type[i] != CKM_SHA256) &&
            (type[i] != CKM_SHA384) &&
            (type[i] != CKM_SHA512) &&
            (type[i] != CKM_MD5) &&
            (type[i] != CKM_MD2)) {
            listNeedLogin = PR_TRUE;
            break;
        }
    }

    for (le = PK11_GetFirstSafe(list); le;
         le = PK11_GetNextSafe(list, le, PR_TRUE)) {
        if (PK11_IsPresent(le->slot)) {
            PRBool doExit = PR_FALSE;
            for (i = 0; i < mech_count; i++) {
                if (!PK11_DoesMechanism(le->slot, type[i])) {
                    doExit = PR_TRUE;
                    break;
                }
                if ((mechanismInfoFlags && mechanismInfoFlags[i]) ||
                    (keySize && keySize[i])) {
                    if (pk11_filterSlot(le->slot, type[i],
                                        mechanismInfoFlags ? mechanismInfoFlags[i] : 0,
                                        keySize ? keySize[i] : 0)) {
                        doExit = PR_TRUE;
                        break;
                    }
                }
            }

            if (doExit)
                continue;

            if (listNeedLogin && le->slot->needLogin) {
                rv = PK11_Authenticate(le->slot, PR_TRUE, wincx);
                if (rv != SECSuccess)
                    continue;
            }
            slot = le->slot;
            PK11_ReferenceSlot(slot);
            PK11_FreeSlotListElement(list, le);
            if (freeit) {
                PK11_FreeSlotList(list);
            }
            return slot;
        }
    }
    if (freeit) {
        PK11_FreeSlotList(list);
    }
    if (PORT_GetError() == 0) {
        PORT_SetError(SEC_ERROR_NO_TOKEN);
    }
    return NULL;
}

PK11SlotInfo *
PK11_GetBestSlotMultiple(CK_MECHANISM_TYPE *type,
                         unsigned int mech_count, void *wincx)
{
    return PK11_GetBestSlotMultipleWithAttributes(type, NULL, NULL,
                                                  mech_count, wincx);
}

/* original get best slot now calls the multiple version with only one type */
PK11SlotInfo *
PK11_GetBestSlot(CK_MECHANISM_TYPE type, void *wincx)
{
    return PK11_GetBestSlotMultipleWithAttributes(&type, NULL, NULL, 1, wincx);
}

PK11SlotInfo *
PK11_GetBestSlotWithAttributes(CK_MECHANISM_TYPE type, CK_FLAGS mechanismFlags,
                               unsigned int keySize, void *wincx)
{
    return PK11_GetBestSlotMultipleWithAttributes(&type, &mechanismFlags,
                                                  &keySize, 1, wincx);
}

int
PK11_GetBestKeyLength(PK11SlotInfo *slot, CK_MECHANISM_TYPE mechanism)
{
    CK_MECHANISM_INFO mechanism_info;
    CK_RV crv;

    if (!slot->isThreadSafe)
        PK11_EnterSlotMonitor(slot);
    crv = PK11_GETTAB(slot)->C_GetMechanismInfo(slot->slotID,
                                                mechanism, &mechanism_info);
    if (!slot->isThreadSafe)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK)
        return 0;

    if (mechanism_info.ulMinKeySize == mechanism_info.ulMaxKeySize)
        return 0;
    return mechanism_info.ulMaxKeySize;
}

/*
 * This function uses the existing PKCS #11 module to find the
 * longest supported key length in the preferred token for a mechanism.
 * This varies from the above function in that 1) it returns the key length
 * even for fixed key algorithms, and 2) it looks through the tokens
 * generally rather than for a specific token. This is used in liu of
 * a PK11_GetKeyLength function in pk11mech.c since we can actually read
 * supported key lengths from PKCS #11.
 *
 * For symmetric key operations the length is returned in bytes.
 */
int
PK11_GetMaxKeyLength(CK_MECHANISM_TYPE mechanism)
{
    CK_MECHANISM_INFO mechanism_info;
    PK11SlotList *list = NULL;
    PK11SlotListElement *le;
    PRBool freeit = PR_FALSE;
    int keyLength = 0;

    list = PK11_GetSlotList(mechanism);

    if ((list == NULL) || (list->head == NULL)) {
        /* We need to look up all the tokens for the mechanism */
        list = PK11_GetAllTokens(mechanism, PR_FALSE, PR_FALSE, NULL);
        freeit = PR_TRUE;
    }

    /* no tokens recognize this mechanism */
    if (list == NULL) {
        PORT_SetError(SEC_ERROR_INVALID_ALGORITHM);
        return 0;
    }

    for (le = PK11_GetFirstSafe(list); le;
         le = PK11_GetNextSafe(list, le, PR_TRUE)) {
        PK11SlotInfo *slot = le->slot;
        CK_RV crv;
        if (PK11_IsPresent(slot)) {
            if (!slot->isThreadSafe)
                PK11_EnterSlotMonitor(slot);
            crv = PK11_GETTAB(slot)->C_GetMechanismInfo(slot->slotID,
                                                        mechanism, &mechanism_info);
            if (!slot->isThreadSafe)
                PK11_ExitSlotMonitor(slot);
            if ((crv == CKR_OK) && (mechanism_info.ulMaxKeySize != 0) && (mechanism_info.ulMaxKeySize != 0xffffffff)) {
                keyLength = mechanism_info.ulMaxKeySize;
                break;
            }
        }
    }

    /* fallback to pk11_GetPredefinedKeyLength for fixed key size algorithms */
    if (keyLength == 0) {
        CK_KEY_TYPE keyType;
        keyType = PK11_GetKeyType(mechanism, 0);
        keyLength = pk11_GetPredefinedKeyLength(keyType);
    }

    if (le)
        PK11_FreeSlotListElement(list, le);
    if (freeit)
        PK11_FreeSlotList(list);
    return keyLength;
}

SECStatus
PK11_SeedRandom(PK11SlotInfo *slot, unsigned char *data, int len)
{
    CK_RV crv;

    PK11_EnterSlotMonitor(slot);
    crv = PK11_GETTAB(slot)->C_SeedRandom(slot->session, data, (CK_ULONG)len);
    PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }
    return SECSuccess;
}

SECStatus
PK11_GenerateRandomOnSlot(PK11SlotInfo *slot, unsigned char *data, int len)
{
    CK_RV crv;

    if (!slot->isInternal)
        PK11_EnterSlotMonitor(slot);
    crv = PK11_GETTAB(slot)->C_GenerateRandom(slot->session, data,
                                              (CK_ULONG)len);
    if (!slot->isInternal)
        PK11_ExitSlotMonitor(slot);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }
    return SECSuccess;
}

/* Attempts to update the Best Slot for "FAKE RANDOM" generation.
** If that's not the internal slot, then it also attempts to update the
** internal slot.
** The return value indicates if the INTERNAL slot was updated OK.
*/
SECStatus
PK11_RandomUpdate(void *data, size_t bytes)
{
    PK11SlotInfo *slot;
    PRBool bestIsInternal;
    SECStatus status;

    slot = PK11_GetBestSlot(CKM_FAKE_RANDOM, NULL);
    if (slot == NULL) {
        slot = PK11_GetInternalSlot();
        if (!slot)
            return SECFailure;
    }

    bestIsInternal = PK11_IsInternal(slot);
    status = PK11_SeedRandom(slot, data, bytes);
    PK11_FreeSlot(slot);

    if (!bestIsInternal) {
        /* do internal slot, too. */
        slot = PK11_GetInternalSlot();
        PORT_Assert(slot);
        if (!slot) {
            return SECFailure;
        }
        status = PK11_SeedRandom(slot, data, bytes);
        PK11_FreeSlot(slot);
    }
    return status;
}

SECStatus
PK11_GenerateRandom(unsigned char *data, int len)
{
    PK11SlotInfo *slot;
    SECStatus rv;

    slot = PK11_GetBestSlot(CKM_FAKE_RANDOM, NULL);
    if (slot == NULL)
        return SECFailure;

    rv = PK11_GenerateRandomOnSlot(slot, data, len);
    PK11_FreeSlot(slot);
    return rv;
}

/*
 * Reset the token to it's initial state. For the internal module, this will
 * Purge your keydb, and reset your cert db certs to USER_INIT.
 */
SECStatus
PK11_ResetToken(PK11SlotInfo *slot, char *sso_pwd)
{
    unsigned char tokenName[32];
    size_t tokenNameLen;
    CK_RV crv;

    /* reconstruct the token name */
    tokenNameLen = PORT_Strlen(slot->token_name);
    if (tokenNameLen > sizeof(tokenName)) {
        tokenNameLen = sizeof(tokenName);
    }

    PORT_Memcpy(tokenName, slot->token_name, tokenNameLen);
    if (tokenNameLen < sizeof(tokenName)) {
        PORT_Memset(&tokenName[tokenNameLen], ' ',
                    sizeof(tokenName) - tokenNameLen);
    }

    /* initialize the token */
    PK11_EnterSlotMonitor(slot);

    /* first shutdown the token. Existing sessions will get closed here */
    PK11_GETTAB(slot)
        ->C_CloseAllSessions(slot->slotID);
    slot->session = CK_INVALID_HANDLE;

    /* now re-init the token */
    crv = PK11_GETTAB(slot)->C_InitToken(slot->slotID,
                                         (unsigned char *)sso_pwd, sso_pwd ? PORT_Strlen(sso_pwd) : 0, tokenName);
    PK11_ExitSlotMonitor(slot);

    /* finally bring the token back up. PK11_InitToken takes the slot monitor
     * itself, so it must be called without the monitor held. */
    PK11_InitToken(slot, PR_TRUE);
    if (crv != CKR_OK) {
        PORT_SetError(PK11_MapError(crv));
        return SECFailure;
    }
    NSSToken *token = PK11Slot_GetNSSToken(slot);
    if (token) {
        nssTrustDomain_UpdateCachedTokenCerts(token->trustDomain, token);
        (void)nssToken_Destroy(token);
    }
    return SECSuccess;
}

void
PK11Slot_SetNSSToken(PK11SlotInfo *sl, NSSToken *nsst)
{
    NSSToken *old;
    if (nsst) {
        nsst = nssToken_AddRef(nsst);
    }

    PR_Lock(sl->nssTokenLock);
    old = sl->nssToken;
    sl->nssToken = nsst;
    PR_Unlock(sl->nssTokenLock);

    if (old) {
        (void)nssToken_Destroy(old);
    }
}

NSSToken *
PK11Slot_GetNSSToken(PK11SlotInfo *sl)
{
    NSSToken *rv = NULL;

    PR_Lock(sl->nssTokenLock);
    if (sl->nssToken) {
        rv = nssToken_AddRef(sl->nssToken);
    }
    PR_Unlock(sl->nssTokenLock);

    return rv;
}

PRBool
pk11slot_GetFIPSStatus(PK11SlotInfo *slot, CK_SESSION_HANDLE session,
                       CK_OBJECT_HANDLE object, CK_ULONG operationType)
{
    SECMODModule *mod = slot->module;
    CK_RV crv;
    CK_ULONG fipsState = CKS_NSS_FIPS_NOT_OK;

    if (PK11_CheckPKCS11Version(slot, 3, 2, PR_TRUE) >= 0) {
        CK_FLAGS validationFlags = 0;

        /* module isn't validated */
        if (slot->validationFIPSFlags == 0) {
            return PR_FALSE;
        }
        switch (operationType) {
            /* in pkcs #11, these are equivalent */
            case CKT_NSS_SESSION_LAST_CHECK:
            case CKT_NSS_SESSION_CHECK:
                crv = PK11_GETTAB(slot)->C_GetSessionValidationFlags(session,
                                                                     CKS_LAST_VALIDATION_OK, &validationFlags);
                if (crv != CKR_OK) {
                    return PR_FALSE;
                }
                break;
            case CKT_NSS_OBJECT_CHECK:
                validationFlags = PK11_ReadULongAttribute(slot, object,
                                                          CKA_OBJECT_VALIDATION_FLAGS);
                if (validationFlags == CK_UNAVAILABLE_INFORMATION) {
                    return PR_FALSE;
                }
                break;
            default:
                return PR_FALSE;
        }
        return (PRBool)(validationFlags & slot->validationFIPSFlags) != 0;
    }
    /* handle the NSS vendor specific indicators, for older modules */
    /* handle the obvious conditions:
     * 1) the module doesn't have a fipsIndicator - fips state must be false */
    if (mod->fipsIndicator == NULL) {
        return PR_FALSE;
    }
    /* 2) the session doesn't exist - fips state must be false */
    if (session == CK_INVALID_HANDLE) {
        return PR_FALSE;
    }

    /* go fetch the state */
    crv = mod->fipsIndicator(session, object, operationType, &fipsState);
    if (crv != CKR_OK) {
        return PR_FALSE;
    }
    return (fipsState == CKS_NSS_FIPS_OK) ? PR_TRUE : PR_FALSE;
}

PRBool
PK11_SlotGetLastFIPSStatus(PK11SlotInfo *slot)
{
    return pk11slot_GetFIPSStatus(slot, slot->session, CK_INVALID_HANDLE,
                                  CKT_NSS_SESSION_LAST_CHECK);
}

/*
 * wait for a token to change it's state. The application passes in the expected
 * new state in event.
 */
PK11TokenStatus
PK11_WaitForTokenEvent(PK11SlotInfo *slot, PK11TokenEvent event,
                       PRIntervalTime timeout, PRIntervalTime latency, int series)
{
    PRIntervalTime first_time = 0;
    PRBool first_time_set = PR_FALSE;
    PRBool waitForRemoval;

    if (slot->isPerm) {
        return PK11TokenNotRemovable;
    }
    if (latency == 0) {
        latency = PR_SecondsToInterval(5);
    }
    waitForRemoval = (PRBool)(event == PK11TokenRemovedOrChangedEvent);

    if (series == 0) {
        series = PK11_GetSlotSeries(slot);
    }
    while (PK11_IsPresent(slot) == waitForRemoval) {
        PRIntervalTime interval;

        if (waitForRemoval && series != PK11_GetSlotSeries(slot)) {
            return PK11TokenChanged;
        }
        if (timeout == PR_INTERVAL_NO_WAIT) {
            return waitForRemoval ? PK11TokenPresent : PK11TokenRemoved;
        }
        if (timeout != PR_INTERVAL_NO_TIMEOUT) {
            interval = PR_IntervalNow();
            if (!first_time_set) {
                first_time = interval;
                first_time_set = PR_TRUE;
            }
            if ((interval - first_time) > timeout) {
                return waitForRemoval ? PK11TokenPresent : PK11TokenRemoved;
            }
        }
        PR_Sleep(latency);
    }
    return waitForRemoval ? PK11TokenRemoved : PK11TokenPresent;
}
