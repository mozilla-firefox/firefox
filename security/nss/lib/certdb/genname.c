/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "plarena.h"
#include "seccomon.h"
#include "secitem.h"
#include "secoidt.h"
#include "secasn1.h"
#include "secder.h"
#include "certt.h"
#include "cert.h"
#include "certi.h"
#include "xconst.h"
#include "secerr.h"
#include "secoid.h"
#include "prprf.h"
#include "genname.h"

SEC_ASN1_MKSUB(SEC_AnyTemplate)
SEC_ASN1_MKSUB(SEC_IntegerTemplate)
SEC_ASN1_MKSUB(SEC_IA5StringTemplate)
SEC_ASN1_MKSUB(SEC_ObjectIDTemplate)
SEC_ASN1_MKSUB(SEC_OctetStringTemplate)

static const SEC_ASN1Template CERTNameConstraintTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTNameConstraint) },
    { SEC_ASN1_ANY, offsetof(CERTNameConstraint, DERName) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 0,
      offsetof(CERTNameConstraint, min), SEC_ASN1_SUB(SEC_IntegerTemplate) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 1,
      offsetof(CERTNameConstraint, max), SEC_ASN1_SUB(SEC_IntegerTemplate) },
    { 0 }
};

const SEC_ASN1Template CERT_NameConstraintSubtreeSubTemplate[] = {
    { SEC_ASN1_SEQUENCE_OF | SEC_ASN1_XTRN, 0, SEC_ASN1_SUB(SEC_AnyTemplate) }
};

static const SEC_ASN1Template CERTNameConstraintsTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(CERTNameConstraints) },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_CONTEXT_SPECIFIC | 0,
      offsetof(CERTNameConstraints, DERPermited),
      CERT_NameConstraintSubtreeSubTemplate },
    { SEC_ASN1_OPTIONAL | SEC_ASN1_CONSTRUCTED | SEC_ASN1_CONTEXT_SPECIFIC | 1,
      offsetof(CERTNameConstraints, DERExcluded),
      CERT_NameConstraintSubtreeSubTemplate },
    { 0 }
};

static const SEC_ASN1Template CERTOthNameTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(OtherName) },
    { SEC_ASN1_OBJECT_ID, offsetof(OtherName, oid) },
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_CONSTRUCTED | SEC_ASN1_EXPLICIT |
          SEC_ASN1_XTRN | 0,
      offsetof(OtherName, name), SEC_ASN1_SUB(SEC_AnyTemplate) },
    { 0 }
};

static const SEC_ASN1Template CERTOtherNameTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_CONSTRUCTED | 0,
      offsetof(CERTGeneralName, name.OthName), CERTOthNameTemplate,
      sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_RFC822NameTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 1,
      offsetof(CERTGeneralName, name.other),
      SEC_ASN1_SUB(SEC_IA5StringTemplate), sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_DNSNameTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 2,
      offsetof(CERTGeneralName, name.other),
      SEC_ASN1_SUB(SEC_IA5StringTemplate), sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_X400AddressTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_CONSTRUCTED | SEC_ASN1_XTRN | 3,
      offsetof(CERTGeneralName, name.other), SEC_ASN1_SUB(SEC_AnyTemplate),
      sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_DirectoryNameTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_CONSTRUCTED | SEC_ASN1_EXPLICIT |
          SEC_ASN1_XTRN | 4,
      offsetof(CERTGeneralName, derDirectoryName),
      SEC_ASN1_SUB(SEC_AnyTemplate), sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_EDIPartyNameTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_CONSTRUCTED | SEC_ASN1_XTRN | 5,
      offsetof(CERTGeneralName, name.other), SEC_ASN1_SUB(SEC_AnyTemplate),
      sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_URITemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 6,
      offsetof(CERTGeneralName, name.other),
      SEC_ASN1_SUB(SEC_IA5StringTemplate), sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_IPAddressTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 7,
      offsetof(CERTGeneralName, name.other),
      SEC_ASN1_SUB(SEC_OctetStringTemplate), sizeof(CERTGeneralName) }
};

static const SEC_ASN1Template CERT_RegisteredIDTemplate[] = {
    { SEC_ASN1_CONTEXT_SPECIFIC | SEC_ASN1_XTRN | 8,
      offsetof(CERTGeneralName, name.other), SEC_ASN1_SUB(SEC_ObjectIDTemplate),
      sizeof(CERTGeneralName) }
};

const SEC_ASN1Template CERT_GeneralNamesTemplate[] = {
    { SEC_ASN1_SEQUENCE_OF | SEC_ASN1_XTRN, 0, SEC_ASN1_SUB(SEC_AnyTemplate) }
};

static struct {
    CERTGeneralNameType type;
    char *name;
} typesArray[] = { { certOtherName, "other" },
                   { certRFC822Name, "email" },
                   { certRFC822Name, "rfc822" },
                   { certDNSName, "dns" },
                   { certX400Address, "x400" },
                   { certX400Address, "x400addr" },
                   { certDirectoryName, "directory" },
                   { certDirectoryName, "dn" },
                   { certEDIPartyName, "edi" },
                   { certEDIPartyName, "ediparty" },
                   { certURI, "uri" },
                   { certIPAddress, "ip" },
                   { certIPAddress, "ipaddr" },
                   { certRegisterID, "registerid" } };

CERTGeneralNameType
CERT_GetGeneralNameTypeFromString(const char *string)
{
    int types_count = sizeof(typesArray) / sizeof(typesArray[0]);
    int i;

    for (i = 0; i < types_count; i++) {
        if (PORT_Strcasecmp(string, typesArray[i].name) == 0) {
            return typesArray[i].type;
        }
    }
    return 0;
}

CERTGeneralName *
CERT_NewGeneralName(PLArenaPool *arena, CERTGeneralNameType type)
{
    CERTGeneralName *name = arena ? PORT_ArenaZNew(arena, CERTGeneralName)
                                  : PORT_ZNew(CERTGeneralName);
    if (name) {
        name->type = type;
        name->l.prev = name->l.next = &name->l;
    }
    return name;
}

/* Copy content of one General Name to another.
** Caller has allocated destination general name.
** This function does not change the destinate's GeneralName's list linkage.
*/
SECStatus
cert_CopyOneGeneralName(PLArenaPool *arena, CERTGeneralName *dest,
                        CERTGeneralName *src)
{
    SECStatus rv;
    void *mark = NULL;

    PORT_Assert(dest != NULL);
    dest->type = src->type;

    mark = PORT_ArenaMark(arena);

    switch (src->type) {
        case certDirectoryName:
            rv = SECITEM_CopyItem(arena, &dest->derDirectoryName,
                                  &src->derDirectoryName);
            if (rv == SECSuccess)
                rv = CERT_CopyName(arena, &dest->name.directoryName,
                                   &src->name.directoryName);
            break;

        case certOtherName:
            rv = SECITEM_CopyItem(arena, &dest->name.OthName.name,
                                  &src->name.OthName.name);
            if (rv == SECSuccess)
                rv = SECITEM_CopyItem(arena, &dest->name.OthName.oid,
                                      &src->name.OthName.oid);
            break;

        default:
            rv = SECITEM_CopyItem(arena, &dest->name.other, &src->name.other);
            break;
    }
    if (rv != SECSuccess) {
        PORT_ArenaRelease(arena, mark);
    } else {
        PORT_ArenaUnmark(arena, mark);
    }
    return rv;
}

void
CERT_DestroyGeneralNameList(CERTGeneralNameList *list)
{
    PRLock *lock;

    if (list != NULL) {
        lock = list->lock;
        PR_Lock(lock);
        PORT_ReleaseAssert(list->refCount > 0);
        if (--list->refCount == 0 && list->arena != NULL) {
            PORT_FreeArena(list->arena, PR_FALSE);
            PR_Unlock(lock);
            PR_DestroyLock(lock);
        } else {
            PR_Unlock(lock);
        }
    }
    return;
}

CERTGeneralNameList *
CERT_CreateGeneralNameList(CERTGeneralName *name)
{
    PLArenaPool *arena;
    CERTGeneralNameList *list = NULL;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        goto done;
    }
    list = PORT_ArenaZNew(arena, CERTGeneralNameList);
    if (!list)
        goto loser;
    if (name != NULL) {
        SECStatus rv;
        list->name = CERT_NewGeneralName(arena, (CERTGeneralNameType)0);
        if (!list->name)
            goto loser;
        rv = CERT_CopyGeneralName(arena, list->name, name);
        if (rv != SECSuccess)
            goto loser;
    }
    list->lock = PR_NewLock();
    if (!list->lock)
        goto loser;
    list->arena = arena;
    list->refCount = 1;
done:
    return list;

loser:
    PORT_FreeArena(arena, PR_FALSE);
    return NULL;
}

CERTGeneralName *
CERT_GetNextGeneralName(CERTGeneralName *current)
{
    PRCList *next;

    next = current->l.next;
    return (CERTGeneralName *)(((char *)next) - offsetof(CERTGeneralName, l));
}

CERTGeneralName *
CERT_GetPrevGeneralName(CERTGeneralName *current)
{
    PRCList *prev;
    prev = current->l.prev;
    return (CERTGeneralName *)(((char *)prev) - offsetof(CERTGeneralName, l));
}

CERTNameConstraint *
CERT_GetNextNameConstraint(CERTNameConstraint *current)
{
    PRCList *next;

    next = current->l.next;
    return (CERTNameConstraint *)(((char *)next) -
                                  offsetof(CERTNameConstraint, l));
}

CERTNameConstraint *
CERT_GetPrevNameConstraint(CERTNameConstraint *current)
{
    PRCList *prev;
    prev = current->l.prev;
    return (CERTNameConstraint *)(((char *)prev) -
                                  offsetof(CERTNameConstraint, l));
}

SECItem *
CERT_EncodeGeneralName(CERTGeneralName *genName, SECItem *dest,
                       PLArenaPool *arena)
{

    const SEC_ASN1Template *template;

    PORT_Assert(arena);
    if (arena == NULL || !genName) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    /* TODO: mark arena */
    if (dest == NULL) {
        dest = PORT_ArenaZNew(arena, SECItem);
        if (!dest)
            goto loser;
    }
    if (genName->type == certDirectoryName) {
        if (genName->derDirectoryName.data == NULL) {
            /* The field hasn't been encoded yet. */
            SECItem *pre_dest = SEC_ASN1EncodeItem(
                arena, &(genName->derDirectoryName),
                &(genName->name.directoryName), CERT_NameTemplate);
            if (!pre_dest)
                goto loser;
        }
        if (genName->derDirectoryName.data == NULL) {
            goto loser;
        }
    }
    switch (genName->type) {
        case certURI:
            template = CERT_URITemplate;
            break;
        case certRFC822Name:
            template = CERT_RFC822NameTemplate;
            break;
        case certDNSName:
            template = CERT_DNSNameTemplate;
            break;
        case certIPAddress:
            template = CERT_IPAddressTemplate;
            break;
        case certOtherName:
            template = CERTOtherNameTemplate;
            break;
        case certRegisterID:
            template = CERT_RegisteredIDTemplate;
            break;
        /* for this type, we expect the value is already encoded */
        case certEDIPartyName:
            template = CERT_EDIPartyNameTemplate;
            break;
        /* for this type, we expect the value is already encoded */
        case certX400Address:
            template = CERT_X400AddressTemplate;
            break;
        case certDirectoryName:
            template = CERT_DirectoryNameTemplate;
            break;
        default:
            PORT_Assert(0);
            goto loser;
    }
    dest = SEC_ASN1EncodeItem(arena, dest, genName, template);
    if (!dest) {
        goto loser;
    }
    /* TODO: unmark arena */
    return dest;
loser:
    /* TODO: release arena back to mark */
    return NULL;
}

SECItem **
cert_EncodeGeneralNames(PLArenaPool *arena, CERTGeneralName *names)
{
    CERTGeneralName *current_name;
    SECItem **items = NULL;
    int count = 1;
    int i;
    PRCList *head;

    if (!names) {
        return NULL;
    }

    PORT_Assert(arena);
    /* TODO: mark arena */
    current_name = names;
    head = &(names->l);
    while (current_name->l.next != head) {
        current_name = CERT_GetNextGeneralName(current_name);
        ++count;
    }
    current_name = CERT_GetNextGeneralName(current_name);
    items = PORT_ArenaNewArray(arena, SECItem *, count + 1);
    if (items == NULL) {
        goto loser;
    }
    for (i = 0; i < count; i++) {
        items[i] = CERT_EncodeGeneralName(current_name, (SECItem *)NULL, arena);
        if (items[i] == NULL) {
            goto loser;
        }
        current_name = CERT_GetNextGeneralName(current_name);
    }
    items[i] = NULL;
    /* TODO: unmark arena */
    return items;
loser:
    /* TODO: release arena to mark */
    return NULL;
}

CERTGeneralName *
CERT_DecodeGeneralName(PLArenaPool *reqArena, SECItem *encodedName,
                       CERTGeneralName *genName)
{
    const SEC_ASN1Template *template;
    CERTGeneralNameType genNameType;
    SECStatus rv = SECSuccess;
    SECItem *newEncodedName;

    if (!reqArena) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    /* make a copy for decoding so the data decoded with QuickDER doesn't
       point to temporary memory */
    newEncodedName = SECITEM_ArenaDupItem(reqArena, encodedName);
    if (!newEncodedName) {
        return NULL;
    }
    /* TODO: mark arena */
    genNameType = (CERTGeneralNameType)((*(newEncodedName->data) & 0x0f) + 1);
    if (genName == NULL) {
        genName = CERT_NewGeneralName(reqArena, genNameType);
        if (!genName)
            goto loser;
    } else {
        genName->type = genNameType;
        genName->l.prev = genName->l.next = &genName->l;
    }

    switch (genNameType) {
        case certURI:
            template = CERT_URITemplate;
            break;
        case certRFC822Name:
            template = CERT_RFC822NameTemplate;
            break;
        case certDNSName:
            template = CERT_DNSNameTemplate;
            break;
        case certIPAddress:
            template = CERT_IPAddressTemplate;
            break;
        case certOtherName:
            template = CERTOtherNameTemplate;
            break;
        case certRegisterID:
            template = CERT_RegisteredIDTemplate;
            break;
        case certEDIPartyName:
            template = CERT_EDIPartyNameTemplate;
            break;
        case certX400Address:
            template = CERT_X400AddressTemplate;
            break;
        case certDirectoryName:
            template = CERT_DirectoryNameTemplate;
            break;
        default:
            goto loser;
    }
    rv = SEC_QuickDERDecodeItem(reqArena, genName, template, newEncodedName);
    if (rv != SECSuccess)
        goto loser;
    if (genNameType == certDirectoryName) {
        rv = SEC_QuickDERDecodeItem(reqArena, &(genName->name.directoryName),
                                    CERT_NameTemplate,
                                    &(genName->derDirectoryName));
        if (rv != SECSuccess)
            goto loser;
    }

    /* TODO: unmark arena */
    return genName;
loser:
    /* TODO: release arena to mark */
    return NULL;
}

CERTGeneralName *
cert_DecodeGeneralNames(PLArenaPool *arena, SECItem **encodedGenName)
{
    PRCList *head = NULL;
    PRCList *tail = NULL;
    CERTGeneralName *currentName = NULL;

    PORT_Assert(arena);
    if (!encodedGenName || !arena) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    /* TODO: mark arena */
    while (*encodedGenName != NULL) {
        currentName = CERT_DecodeGeneralName(arena, *encodedGenName, NULL);
        if (currentName == NULL)
            break;
        if (head == NULL) {
            head = &(currentName->l);
            tail = head;
        }
        currentName->l.next = head;
        currentName->l.prev = tail;
        tail = head->prev = tail->next = &(currentName->l);
        encodedGenName++;
    }
    if (currentName) {
        /* TODO: unmark arena */
        return CERT_GetNextGeneralName(currentName);
    }
    /* TODO: release arena to mark */
    return NULL;
}

void
CERT_DestroyGeneralName(CERTGeneralName *name)
{
    cert_DestroyGeneralNames(name);
}

SECStatus
cert_DestroyGeneralNames(CERTGeneralName *name)
{
    CERTGeneralName *first;
    CERTGeneralName *next = NULL;

    first = name;
    do {
        next = CERT_GetNextGeneralName(name);
        PORT_Free(name);
        name = next;
    } while (name != first);
    return SECSuccess;
}

static SECItem *
cert_EncodeNameConstraint(CERTNameConstraint *constraint, SECItem *dest,
                          PLArenaPool *arena)
{
    PORT_Assert(arena);
    if (dest == NULL) {
        dest = PORT_ArenaZNew(arena, SECItem);
        if (dest == NULL) {
            return NULL;
        }
    }
    CERT_EncodeGeneralName(&(constraint->name), &(constraint->DERName), arena);

    dest =
        SEC_ASN1EncodeItem(arena, dest, constraint, CERTNameConstraintTemplate);
    return dest;
}

SECStatus
cert_EncodeNameConstraintSubTree(CERTNameConstraint *constraints,
                                 PLArenaPool *arena, SECItem ***dest,
                                 PRBool permited)
{
    CERTNameConstraint *current_constraint = constraints;
    SECItem **items = NULL;
    int count = 0;
    int i;
    PRCList *head;

    PORT_Assert(arena);
    /* TODO: mark arena */
    if (constraints != NULL) {
        count = 1;
    }
    head = &constraints->l;
    while (current_constraint->l.next != head) {
        current_constraint = CERT_GetNextNameConstraint(current_constraint);
        ++count;
    }
    current_constraint = CERT_GetNextNameConstraint(current_constraint);
    items = PORT_ArenaZNewArray(arena, SECItem *, count + 1);
    if (items == NULL) {
        goto loser;
    }
    for (i = 0; i < count; i++) {
        items[i] = cert_EncodeNameConstraint(current_constraint,
                                             (SECItem *)NULL, arena);
        if (items[i] == NULL) {
            goto loser;
        }
        current_constraint = CERT_GetNextNameConstraint(current_constraint);
    }
    *dest = items;
    if (*dest == NULL) {
        goto loser;
    }
    /* TODO: unmark arena */
    return SECSuccess;
loser:
    /* TODO: release arena to mark */
    return SECFailure;
}

SECStatus
cert_EncodeNameConstraints(CERTNameConstraints *constraints, PLArenaPool *arena,
                           SECItem *dest)
{
    SECStatus rv = SECSuccess;

    PORT_Assert(arena);
    /* TODO: mark arena */
    if (constraints->permited != NULL) {
        rv = cert_EncodeNameConstraintSubTree(
            constraints->permited, arena, &constraints->DERPermited, PR_TRUE);
        if (rv == SECFailure) {
            goto loser;
        }
    }
    if (constraints->excluded != NULL) {
        rv = cert_EncodeNameConstraintSubTree(
            constraints->excluded, arena, &constraints->DERExcluded, PR_FALSE);
        if (rv == SECFailure) {
            goto loser;
        }
    }
    dest = SEC_ASN1EncodeItem(arena, dest, constraints,
                              CERTNameConstraintsTemplate);
    if (dest == NULL) {
        goto loser;
    }
    /* TODO: unmark arena */
    return SECSuccess;
loser:
    /* TODO: release arena to mark */
    return SECFailure;
}

CERTNameConstraint *
cert_DecodeNameConstraint(PLArenaPool *reqArena, SECItem *encodedConstraint)
{
    CERTNameConstraint *constraint;
    SECStatus rv = SECSuccess;
    CERTGeneralName *temp;
    SECItem *newEncodedConstraint;

    if (!reqArena) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    newEncodedConstraint = SECITEM_ArenaDupItem(reqArena, encodedConstraint);
    if (!newEncodedConstraint) {
        return NULL;
    }
    /* TODO: mark arena */
    constraint = PORT_ArenaZNew(reqArena, CERTNameConstraint);
    if (!constraint)
        goto loser;
    rv = SEC_QuickDERDecodeItem(
        reqArena, constraint, CERTNameConstraintTemplate, newEncodedConstraint);
    if (rv != SECSuccess) {
        goto loser;
    }
    temp = CERT_DecodeGeneralName(reqArena, &(constraint->DERName),
                                  &(constraint->name));
    if (temp != &(constraint->name)) {
        goto loser;
    }

    /* ### sjlee: since the name constraint contains only one
     *            CERTGeneralName, the list within CERTGeneralName shouldn't
     *            point anywhere else.  Otherwise, bad things will happen.
     */
    constraint->name.l.prev = constraint->name.l.next = &(constraint->name.l);
    /* TODO: unmark arena */
    return constraint;
loser:
    /* TODO: release arena back to mark */
    return NULL;
}

static CERTNameConstraint *
cert_DecodeNameConstraintSubTree(PLArenaPool *arena, SECItem **subTree,
                                 PRBool permited)
{
    CERTNameConstraint *current = NULL;
    CERTNameConstraint *first = NULL;
    CERTNameConstraint *last = NULL;
    int i = 0;

    PORT_Assert(arena);
    /* TODO: mark arena */
    while (subTree[i] != NULL) {
        current = cert_DecodeNameConstraint(arena, subTree[i]);
        if (current == NULL) {
            goto loser;
        }
        if (first == NULL) {
            first = current;
        } else {
            current->l.prev = &(last->l);
            last->l.next = &(current->l);
        }
        last = current;
        i++;
    }
    if (first && last) {
        first->l.prev = &(last->l);
        last->l.next = &(first->l);
    }
    /* TODO: unmark arena */
    return first;
loser:
    /* TODO: release arena back to mark */
    return NULL;
}

CERTNameConstraints *
cert_DecodeNameConstraints(PLArenaPool *reqArena,
                           const SECItem *encodedConstraints)
{
    CERTNameConstraints *constraints;
    SECStatus rv;
    SECItem *newEncodedConstraints;

    if (!reqArena) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    PORT_Assert(encodedConstraints);
    newEncodedConstraints = SECITEM_ArenaDupItem(reqArena, encodedConstraints);

    /* TODO: mark arena */
    constraints = PORT_ArenaZNew(reqArena, CERTNameConstraints);
    if (constraints == NULL) {
        goto loser;
    }
    rv = SEC_QuickDERDecodeItem(reqArena, constraints,
                                CERTNameConstraintsTemplate,
                                newEncodedConstraints);
    if (rv != SECSuccess) {
        goto loser;
    }
    if (constraints->DERPermited != NULL &&
        constraints->DERPermited[0] != NULL) {
        constraints->permited = cert_DecodeNameConstraintSubTree(
            reqArena, constraints->DERPermited, PR_TRUE);
        if (constraints->permited == NULL) {
            goto loser;
        }
    }
    if (constraints->DERExcluded != NULL &&
        constraints->DERExcluded[0] != NULL) {
        constraints->excluded = cert_DecodeNameConstraintSubTree(
            reqArena, constraints->DERExcluded, PR_FALSE);
        if (constraints->excluded == NULL) {
            goto loser;
        }
    }
    /* TODO: unmark arena */
    return constraints;
loser:
    /* TODO: release arena back to mark */
    return NULL;
}

/* Copy a chain of one or more general names to a destination chain.
** Caller has allocated at least the first destination GeneralName struct.
** Both source and destination chains are circular doubly-linked lists.
** The first source struct is copied to the first destination struct.
** If the source chain has more than one member, and the destination chain
** has only one member, then this function allocates new structs for all but
** the first copy from the arena and links them into the destination list.
** If the destination struct is part of a list with more than one member,
** then this function traverses both the source and destination lists,
** copying each source struct to the corresponding dest struct.
** In that case, the destination list MUST contain at least as many
** structs as the source list or some dest entries will be overwritten.
*/
SECStatus
CERT_CopyGeneralName(PLArenaPool *arena, CERTGeneralName *dest,
                     CERTGeneralName *src)
{
    SECStatus rv;
    CERTGeneralName *destHead = dest;
    CERTGeneralName *srcHead = src;

    PORT_Assert(dest != NULL);
    if (!dest) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    /* TODO: mark arena */
    do {
        rv = cert_CopyOneGeneralName(arena, dest, src);
        if (rv != SECSuccess)
            goto loser;
        src = CERT_GetNextGeneralName(src);
        /* if there is only one general name, we shouldn't do this */
        if (src != srcHead) {
            if (dest->l.next == &destHead->l) {
                CERTGeneralName *temp;
                temp = CERT_NewGeneralName(arena, (CERTGeneralNameType)0);
                if (!temp)
                    goto loser;
                temp->l.next = &destHead->l;
                temp->l.prev = &dest->l;
                destHead->l.prev = &temp->l;
                dest->l.next = &temp->l;
                dest = temp;
            } else {
                dest = CERT_GetNextGeneralName(dest);
            }
        }
    } while (src != srcHead && rv == SECSuccess);
    /* TODO: unmark arena */
    return rv;
loser:
    /* TODO: release back to mark */
    return SECFailure;
}

/* Allocate space and copy CERTNameConstraint from src to dest */
CERTNameConstraint *
CERT_CopyNameConstraint(PLArenaPool *arena, CERTNameConstraint *dest,
                        CERTNameConstraint *src)
{
    SECStatus rv;

    /* TODO: mark arena */
    if (dest == NULL) {
        dest = PORT_ArenaZNew(arena, CERTNameConstraint);
        if (!dest)
            goto loser;
        /* mark that it is not linked */
        dest->name.l.prev = dest->name.l.next = &(dest->name.l);
    }
    rv = CERT_CopyGeneralName(arena, &dest->name, &src->name);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = SECITEM_CopyItem(arena, &dest->DERName, &src->DERName);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = SECITEM_CopyItem(arena, &dest->min, &src->min);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = SECITEM_CopyItem(arena, &dest->max, &src->max);
    if (rv != SECSuccess) {
        goto loser;
    }
    dest->l.prev = dest->l.next = &dest->l;
    /* TODO: unmark arena */
    return dest;
loser:
    /* TODO: release arena to mark */
    return NULL;
}

CERTGeneralName *
cert_CombineNamesLists(CERTGeneralName *list1, CERTGeneralName *list2)
{
    PRCList *begin1;
    PRCList *begin2;
    PRCList *end1;
    PRCList *end2;

    if (list1 == NULL) {
        return list2;
    } else if (list2 == NULL) {
        return list1;
    } else {
        begin1 = &list1->l;
        begin2 = &list2->l;
        end1 = list1->l.prev;
        end2 = list2->l.prev;
        end1->next = begin2;
        end2->next = begin1;
        begin1->prev = end2;
        begin2->prev = end1;
        return list1;
    }
}

CERTNameConstraint *
cert_CombineConstraintsLists(CERTNameConstraint *list1,
                             CERTNameConstraint *list2)
{
    PRCList *begin1;
    PRCList *begin2;
    PRCList *end1;
    PRCList *end2;

    if (list1 == NULL) {
        return list2;
    } else if (list2 == NULL) {
        return list1;
    } else {
        begin1 = &list1->l;
        begin2 = &list2->l;
        end1 = list1->l.prev;
        end2 = list2->l.prev;
        end1->next = begin2;
        end2->next = begin1;
        begin1->prev = end2;
        begin2->prev = end1;
        return list1;
    }
}

/* Add a CERTNameConstraint to the CERTNameConstraint list */
CERTNameConstraint *
CERT_AddNameConstraint(CERTNameConstraint *list, CERTNameConstraint *constraint)
{
    PORT_Assert(constraint != NULL);
    constraint->l.next = constraint->l.prev = &constraint->l;
    list = cert_CombineConstraintsLists(list, constraint);
    return list;
}

SECStatus
CERT_GetNameConstraintByType(CERTNameConstraint *constraints,
                             CERTGeneralNameType type,
                             CERTNameConstraint **returnList,
                             PLArenaPool *arena)
{
    CERTNameConstraint *current = NULL;
    void *mark = NULL;

    *returnList = NULL;
    if (!constraints)
        return SECSuccess;

    mark = PORT_ArenaMark(arena);

    current = constraints;
    do {
        PORT_Assert(current->name.type);
        if (current->name.type == type) {
            CERTNameConstraint *temp;
            temp = CERT_CopyNameConstraint(arena, NULL, current);
            if (temp == NULL)
                goto loser;
            *returnList = CERT_AddNameConstraint(*returnList, temp);
        }
        current = CERT_GetNextNameConstraint(current);
    } while (current != constraints);
    PORT_ArenaUnmark(arena, mark);
    return SECSuccess;

loser:
    PORT_ArenaRelease(arena, mark);
    return SECFailure;
}

void *
CERT_GetGeneralNameByType(CERTGeneralName *genNames, CERTGeneralNameType type,
                          PRBool derFormat)
{
    CERTGeneralName *current;

    if (!genNames)
        return NULL;
    current = genNames;

    do {
        if (current->type == type) {
            switch (type) {
                case certDNSName:
                case certEDIPartyName:
                case certIPAddress:
                case certRegisterID:
                case certRFC822Name:
                case certX400Address:
                case certURI:
                    return (void *)&current->name.other; /* SECItem * */

                case certOtherName:
                    return (void *)&current->name.OthName; /* OthName * */

                case certDirectoryName:
                    return derFormat
                               ? (void *)&current
                                     ->derDirectoryName /* SECItem * */
                               : (void *)&current->name
                                     .directoryName; /* CERTName * */
            }
            PORT_Assert(0);
            return NULL;
        }
        current = CERT_GetNextGeneralName(current);
    } while (current != genNames);
    return NULL;
}

int
CERT_GetNamesLength(CERTGeneralName *names)
{
    int length = 0;
    CERTGeneralName *first;

    first = names;
    if (names != NULL) {
        do {
            length++;
            names = CERT_GetNextGeneralName(names);
        } while (names != first);
    }
    return length;
}

/* Creates new GeneralNames for any email addresses found in the
** input DN, and links them onto the list for the DN.
*/
SECStatus
cert_ExtractDNEmailAddrs(CERTGeneralName *name, PLArenaPool *arena)
{
    CERTGeneralName *nameList = NULL;
    const CERTRDN **nRDNs = (const CERTRDN **)(name->name.directoryName.rdns);
    SECStatus rv = SECSuccess;

    PORT_Assert(name->type == certDirectoryName);
    if (name->type != certDirectoryName) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }
    /* TODO: mark arena */
    while (nRDNs && *nRDNs) { /* loop over RDNs */
        const CERTRDN *nRDN = *nRDNs++;
        CERTAVA **nAVAs = nRDN->avas;
        while (nAVAs && *nAVAs) { /* loop over AVAs */
            int tag;
            CERTAVA *nAVA = *nAVAs++;
            tag = CERT_GetAVATag(nAVA);
            if (tag == SEC_OID_PKCS9_EMAIL_ADDRESS ||
                tag == SEC_OID_RFC1274_MAIL) { /* email AVA */
                CERTGeneralName *newName = NULL;
                SECItem *avaValue = CERT_DecodeAVAValue(&nAVA->value);
                if (!avaValue)
                    goto loser;
                rv = SECFailure;
                newName = CERT_NewGeneralName(arena, certRFC822Name);
                if (newName) {
                    rv =
                        SECITEM_CopyItem(arena, &newName->name.other, avaValue);
                }
                SECITEM_FreeItem(avaValue, PR_TRUE);
                if (rv != SECSuccess)
                    goto loser;
                nameList = cert_CombineNamesLists(nameList, newName);
            } /* handle one email AVA */
        } /* loop over AVAs */
    } /* loop over RDNs */
    /* combine new names with old one. */
    (void)cert_CombineNamesLists(name, nameList);
    /* TODO: unmark arena */
    return SECSuccess;

loser:
    /* TODO: release arena back to mark */
    return SECFailure;
}

/* Extract all names except Subject Common Name from a cert
** in preparation for a name constraints test.
*/
CERTGeneralName *
CERT_GetCertificateNames(CERTCertificate *cert, PLArenaPool *arena)
{
    return CERT_GetConstrainedCertificateNames(cert, arena, PR_FALSE);
}

/* This function is called by CERT_VerifyCertChain to extract all
** names from a cert in preparation for a name constraints test.
*/
CERTGeneralName *
CERT_GetConstrainedCertificateNames(const CERTCertificate *cert,
                                    PLArenaPool *arena,
                                    PRBool includeSubjectCommonName)
{
    CERTGeneralName *DN;
    CERTGeneralName *SAN;
    PRUint32 numDNSNames = 0;
    SECStatus rv;

    if (!arena) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }
    /* TODO: mark arena */
    DN = CERT_NewGeneralName(arena, certDirectoryName);
    if (DN == NULL) {
        goto loser;
    }
    rv = CERT_CopyName(arena, &DN->name.directoryName, &cert->subject);
    if (rv != SECSuccess) {
        goto loser;
    }
    rv = SECITEM_CopyItem(arena, &DN->derDirectoryName, &cert->derSubject);
    if (rv != SECSuccess) {
        goto loser;
    }
    /* Extract email addresses from DN, construct CERTGeneralName structs
    ** for them, add them to the name list
    */
    rv = cert_ExtractDNEmailAddrs(DN, arena);
    if (rv != SECSuccess)
        goto loser;

    /* Now extract any GeneralNames from the subject name names extension. */
    SAN = cert_GetSubjectAltNameList(cert, arena);
    if (SAN) {
        numDNSNames = cert_CountDNSPatterns(SAN);
        DN = cert_CombineNamesLists(DN, SAN);
    }
    if (!numDNSNames && includeSubjectCommonName) {
        char *cn = CERT_GetCommonName(&cert->subject);
        if (cn) {
            CERTGeneralName *CN = CERT_NewGeneralName(arena, certDNSName);
            if (CN) {
                SECItem cnItem = { siBuffer, NULL, 0 };
                cnItem.data = (unsigned char *)cn;
                cnItem.len = strlen(cn);
                rv = SECITEM_CopyItem(arena, &CN->name.other, &cnItem);
                if (rv == SECSuccess) {
                    DN = cert_CombineNamesLists(DN, CN);
                }
            }
            PORT_Free(cn);
        }
    }
    if (rv == SECSuccess) {
        /* TODO: unmark arena */
        return DN;
    }
loser:
    /* TODO: release arena to mark */
    return NULL;
}

/* Returns SECSuccess if name matches constraint per RFC 3280 rules for
** URI name constraints.  SECFailure otherwise.
** If the constraint begins with a dot, it is a domain name, otherwise
** It is a host name.  Examples:
**  Constraint            Name             Result
** ------------      ---------------      --------
**  foo.bar.com          foo.bar.com      matches
**  foo.bar.com          FoO.bAr.CoM      matches
**  foo.bar.com      www.foo.bar.com      no match
**  foo.bar.com        nofoo.bar.com      no match
** .foo.bar.com      www.foo.bar.com      matches
** .foo.bar.com        nofoo.bar.com      no match
** .foo.bar.com          foo.bar.com      no match
** .foo.bar.com     www..foo.bar.com      no match
*/
static SECStatus
compareURIN2C(const SECItem *name, const SECItem *constraint)
{
    int offset;
    /* The spec is silent on intepreting zero-length constraints.
    ** We interpret them as matching no URI names.
    */
    if (!constraint->len)
        return SECFailure;
    if (constraint->data[0] != '.') {
        /* constraint is a host name. */
        if (name->len != constraint->len ||
            PL_strncasecmp((char *)name->data, (char *)constraint->data,
                           constraint->len))
            return SECFailure;
        return SECSuccess;
    }
    /* constraint is a domain name. */
    if (name->len < constraint->len)
        return SECFailure;
    offset = name->len - constraint->len;
    if (PL_strncasecmp((char *)(name->data + offset), (char *)constraint->data,
                       constraint->len))
        return SECFailure;
    if (!offset ||
        (name->data[offset - 1] == '.') + (constraint->data[0] == '.') == 1)
        return SECSuccess;
    return SECFailure;
}

/* for DNSname constraints, RFC 3280 says, (section 4.2.1.11, page 38)
**
** DNS name restrictions are expressed as foo.bar.com.  Any DNS name
** that can be constructed by simply adding to the left hand side of the
** name satisfies the name constraint.  For example, www.foo.bar.com
** would satisfy the constraint but foo1.bar.com would not.
**
** But NIST's PKITS test suite requires that the constraint be treated
** as a domain name, and requires that any name added to the left hand
** side end in a dot ".".  Sensible, but not strictly following the RFC.
**
**  Constraint            Name            RFC 3280  NIST PKITS
** ------------      ---------------      --------  ----------
**  foo.bar.com          foo.bar.com      matches    matches
**  foo.bar.com          FoO.bAr.CoM      matches    matches
**  foo.bar.com      www.foo.bar.com      matches    matches
**  foo.bar.com        nofoo.bar.com      MATCHES    NO MATCH
** .foo.bar.com      www.foo.bar.com      matches    matches? disallowed?
** .foo.bar.com          foo.bar.com      no match   no match
** .foo.bar.com     www..foo.bar.com      matches    probably not
**
** We will try to conform to NIST's PKITS tests, and the unstated
** rules they imply.
*/
static SECStatus
compareDNSN2C(const SECItem *name, const SECItem *constraint)
{
    int offset;
    /* The spec is silent on intepreting zero-length constraints.
    ** We interpret them as matching all DNSnames.
    */
    if (!constraint->len)
        return SECSuccess;
    if (name->len < constraint->len)
        return SECFailure;
    offset = name->len - constraint->len;
    if (PL_strncasecmp((char *)(name->data + offset), (char *)constraint->data,
                       constraint->len))
        return SECFailure;
    if (!offset ||
        (name->data[offset - 1] == '.') + (constraint->data[0] == '.') == 1)
        return SECSuccess;
    return SECFailure;
}

/* Returns SECSuccess if name matches constraint per RFC 3280 rules for
** internet email addresses.  SECFailure otherwise.
** If constraint contains a '@' then the two strings much match exactly.
** Else if constraint starts with a '.'. then it must match the right-most
** substring of the name,
** else constraint string must match entire name after the name's '@'.
** Empty constraint string matches all names. All comparisons case insensitive.
*/
static SECStatus
compareRFC822N2C(const SECItem *name, const SECItem *constraint)
{
    int offset;
    if (!constraint->len)
        return SECSuccess;
    if (name->len < constraint->len)
        return SECFailure;
    if (constraint->len == 1 && constraint->data[0] == '.')
        return SECSuccess;
    for (offset = constraint->len - 1; offset >= 0; --offset) {
        if (constraint->data[offset] == '@') {
            return (name->len == constraint->len &&
                    !PL_strncasecmp((char *)name->data,
                                    (char *)constraint->data, constraint->len))
                       ? SECSuccess
                       : SECFailure;
        }
    }
    offset = name->len - constraint->len;
    if (PL_strncasecmp((char *)(name->data + offset), (char *)constraint->data,
                       constraint->len))
        return SECFailure;
    if (constraint->data[0] == '.')
        return SECSuccess;
    if (offset > 0 && name->data[offset - 1] == '@')
        return SECSuccess;
    return SECFailure;
}

/* Returns SECSuccess if name is inside the permitted directoryName subtree
** constraint, per RFC 5280 section 4.2.1.10: the constraint is a prefix of
** name, i.e. name has at least as many RDNs as the constraint, and each RDN
** of the constraint is equal to the RDN at the same position in name.  A
** constraint with no RDNs matches every name.  An empty name (an empty
** subject) is not subject to directoryName constraints and matches too.
** SECFailure otherwise.
*/
static SECStatus
comparePermittedDirectoryNameN2C(const CERTName *name,
                                 const CERTName *constraint)
{
    CERTRDN **nRDNs = name->rdns;
    CERTRDN **cRDNs = constraint->rdns;

    if (!nRDNs || !*nRDNs) {
        return SECSuccess;
    }
    while (cRDNs && *cRDNs) {
        if (!nRDNs || !*nRDNs) {
            return SECFailure; /* name is shorter than the constraint */
        }
        if (CERT_CompareRDN(*cRDNs++, *nRDNs++) != SECEqual) {
            return SECFailure;
        }
    }
    return SECSuccess;
}

/* Returns SECSuccess if name is inside the excluded directoryName subtree
** constraint.  This match is broader than the prefix match above: RDNs are
** compared only as far as both names go, and a name RDN matches when it holds
** every attribute of the constraint RDN.  It covers every name the prefix
** match covers, so an excluded subtree never lets out a name that is inside
** it.  SECFailure otherwise.
*/
static SECStatus
compareExcludedDirectoryNameN2C(const CERTName *name,
                                const CERTName *constraint)
{
    /* status defaults to SECEqual, so that a constraint with
    ** no AVAs will be a wildcard, matching all directory names.
    */
    SECComparison status = SECEqual;
    const CERTRDN **cRDNs = (const CERTRDN **)constraint->rdns;
    const CERTRDN **nRDNs = (const CERTRDN **)name->rdns;
    while (cRDNs && *cRDNs && nRDNs && *nRDNs) {
        /* loop over name RDNs and constraint RDNs in lock step
         */
        const CERTRDN *cRDN = *cRDNs++;
        const CERTRDN *nRDN = *nRDNs++;
        CERTAVA **cAVAs = cRDN->avas;
        while (cAVAs && *cAVAs) { /* loop over constraint AVAs */
            CERTAVA *cAVA = *cAVAs++;
            CERTAVA **nAVAs = nRDN->avas;
            while (nAVAs && *nAVAs) { /* loop over name AVAs */
                CERTAVA *nAVA = *nAVAs++;
                status = CERT_CompareAVA(cAVA, nAVA);
                if (status == SECEqual)
                    break;
            } /* loop over name AVAs */
            if (status != SECEqual)
                break;
        } /* loop over constraint AVAs */
        if (status != SECEqual)
            break;
    } /* loop over name RDNs and constraint RDNs */
    return (status == SECEqual) ? SECSuccess : SECFailure;
}

/* name contains either a 4 byte IPv4 address or a 16 byte IPv6 address.
** constraint contains an address of the same length, and a subnet mask
** of the same length.  Compare name's address to the constraint's
** address, subject to the mask.
** Return SECSuccess if they match, SECFailure if they don't.
*/
static SECStatus
compareIPaddrN2C(const SECItem *name, const SECItem *constraint)
{
    int i;
    if (name->len == 4 && constraint->len == 8) { /* ipv4 addr */
        for (i = 0; i < 4; i++) {
            if ((name->data[i] ^ constraint->data[i]) & constraint->data[i + 4])
                goto loser;
        }
        return SECSuccess;
    }
    if (name->len == 16 && constraint->len == 32) { /* ipv6 addr */
        for (i = 0; i < 16; i++) {
            if ((name->data[i] ^ constraint->data[i]) &
                constraint->data[i + 16])
                goto loser;
        }
        return SECSuccess;
    }
loser:
    return SECFailure;
}

/* start with a SECItem that points to a URI.  Parse it lookingg for
** a hostname.  Modify item->data and item->len to define the hostname,
** but do not modify and data at item->data.
** If anything goes wrong, the contents of *item are undefined.
*/
static SECStatus
parseUriHostname(SECItem *item)
{
    int i;
    PRBool found = PR_FALSE;
    for (i = 0; (unsigned)(i + 2) < item->len; ++i) {
        if (item->data[i] == ':' && item->data[i + 1] == '/' &&
            item->data[i + 2] == '/') {
            i += 3;
            item->data += i;
            item->len -= i;
            found = PR_TRUE;
            break;
        }
    }
    if (!found)
        return SECFailure;
    /* now look for a '/', which is an upper bound in the end of the name */
    for (i = 0; (unsigned)i < item->len; ++i) {
        if (item->data[i] == '/') {
            item->len = i;
            break;
        }
    }
    /* now look for a ':', which marks the end of the name */
    for (i = item->len; --i >= 0;) {
        if (item->data[i] == ':') {
            item->len = i;
            break;
        }
    }
    /* now look for an '@', which marks the beginning of the hostname */
    for (i = 0; (unsigned)i < item->len; ++i) {
        if (item->data[i] == '@') {
            ++i;
            item->data += i;
            item->len -= i;
            break;
        }
    }
    return item->len ? SECSuccess : SECFailure;
}

/* This function takes one name, and a list of constraints.
** It searches the constraints looking for a match.
** It returns SECSuccess if the name satisfies the constraints, i.e.,
** if excluded, then the name does not match any constraint,
** if permitted, then the name matches at least one constraint.
** It returns SECFailure if the name fails to satisfy the constraints,
** or if some code fails (e.g. out of memory, or invalid constraint)
*/
SECStatus
cert_CompareNameWithConstraints(const CERTGeneralName *name,
                                const CERTNameConstraint *constraints,
                                PRBool excluded)
{
    SECStatus rv = SECSuccess;
    SECStatus matched = SECFailure;
    const CERTNameConstraint *current;

    PORT_Assert(constraints); /* caller should not call with NULL */
    if (!constraints) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    current = constraints;
    do {
        rv = SECSuccess;
        matched = SECFailure;
        PORT_Assert(name->type == current->name.type);
        switch (name->type) {

            case certDNSName:
                matched =
                    compareDNSN2C(&name->name.other, &current->name.name.other);
                break;

            case certRFC822Name:
                matched = compareRFC822N2C(&name->name.other,
                                           &current->name.name.other);
                break;

            case certURI: {
                /* make a modifiable copy of the URI SECItem. */
                SECItem uri = name->name.other;
                /* find the hostname in the URI */
                rv = parseUriHostname(&uri);
                if (rv == SECSuccess) {
                    /* does our hostname meet the constraint? */
                    matched = compareURIN2C(&uri, &current->name.name.other);
                }
            } break;

            case certDirectoryName:
                matched = excluded ? compareExcludedDirectoryNameN2C(
                                         &name->name.directoryName,
                                         &current->name.name.directoryName)
                                   : comparePermittedDirectoryNameN2C(
                                         &name->name.directoryName,
                                         &current->name.name.directoryName);
                break;

            case certIPAddress: /* type 8 */
                matched = compareIPaddrN2C(&name->name.other,
                                           &current->name.name.other);
                break;

            /* NSS does not know how to compare these "Other" type names with
            ** their respective constraints.  But it does know how to tell
            ** if the constraint applies to the type of name (by comparing
            ** the constraint OID to the name OID).  NSS makes no use of "Other"
            ** type names at all, so NSS errs on the side of leniency for these
            ** types, provided that their OIDs match.  So, when an "Other"
            ** name constraint appears in an excluded subtree, it never causes
            ** a name to fail.  When an "Other" name constraint appears in a
            ** permitted subtree, AND the constraint's OID matches the name's
            ** OID, then name is treated as if it matches the constraint.
            */
            case certOtherName: /* type 1 */
                matched =
                    (!excluded && name->type == current->name.type &&
                     SECITEM_ItemsAreEqual(&name->name.OthName.oid,
                                           &current->name.name.OthName.oid))
                        ? SECSuccess
                        : SECFailure;
                break;

            /* NSS does not know how to compare these types of names with their
            ** respective constraints.  But NSS makes no use of these types of
            ** names at all, so it errs on the side of leniency for these types.
            ** Constraints for these types of names never cause the name to
            ** fail the constraints test.  NSS behaves as if the name matched
            ** for permitted constraints, and did not match for excluded ones.
            */
            case certX400Address:  /* type 4 */
            case certEDIPartyName: /* type 6 */
            case certRegisterID:   /* type 9 */
                matched = excluded ? SECFailure : SECSuccess;
                break;

            default: /* non-standard types are not supported */
                rv = SECFailure;
                break;
        }
        if (matched == SECSuccess || rv != SECSuccess)
            break;
        current = CERT_GetNextNameConstraint((CERTNameConstraint *)current);
    } while (current != constraints);
    if (rv == SECSuccess) {
        if (matched == SECSuccess)
            rv = excluded ? SECFailure : SECSuccess;
        else
            rv = excluded ? SECSuccess : SECFailure;
        return rv;
    }

    return SECFailure;
}

/*
 * Here we define a list of name constraints to be imposed on
 * certain certificates, most importantly root certificates.
 *
 * Each entry in the name constraints list is constructed with this
 * macro.  An entry contains two SECItems, which have names in
 * specific forms to make the macro work:
 *
 *  * ${CA}_SUBJECT_DN - The subject DN for which the constraints
 *                       should be applied
 *  * ${CA}_NAME_CONSTRAINTS - The name constraints extension
 *
 * Entities subject to name constraints are identified by subject name
 * so that we can cover all certificates for that entity, including, e.g.,
 * cross-certificates.  We use subject rather than public key because
 * calling methods often have easy access to that field (vs., say, a key ID),
 * and in practice, subject names and public keys are usually in one-to-one
 * correspondence anyway.
 *
 */

#define STRING_TO_SECITEM(str)                          \
    {                                                   \
        siBuffer, (unsigned char *)str, sizeof(str) - 1 \
    }

#define NAME_CONSTRAINTS_ENTRY(CA)               \
    {                                            \
        STRING_TO_SECITEM(CA##_SUBJECT_DN),      \
        STRING_TO_SECITEM(CA##_NAME_CONSTRAINTS) \
    }

/* clang-format off */

/* Agence Nationale de la Securite des Systemes d'Information (ANSSI) */

#define ANSSI_SUBJECT_DN                                                       \
    "\x30\x81\x85"                                                             \
    "\x31\x0B\x30\x09\x06\x03\x55\x04\x06\x13\x02" "FR"       /* C */          \
    "\x31\x0F\x30\x0D\x06\x03\x55\x04\x08\x13\x06" "France"   /* ST */         \
    "\x31\x0E\x30\x0C\x06\x03\x55\x04\x07\x13\x05" "Paris"    /* L */          \
    "\x31\x10\x30\x0E\x06\x03\x55\x04\x0A\x13\x07" "PM/SGDN"  /* O */          \
    "\x31\x0E\x30\x0C\x06\x03\x55\x04\x0B\x13\x05" "DCSSI"    /* OU */         \
    "\x31\x0E\x30\x0C\x06\x03\x55\x04\x03\x13\x05" "IGC/A"    /* CN */         \
    "\x31\x23\x30\x21\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x09\x01"             \
    "\x16\x14" "igca@sgdn.pm.gouv.fr" /* emailAddress */                       \

#define ANSSI_NAME_CONSTRAINTS                                                 \
    "\x30\x5D\xA0\x5B"                                                         \
    "\x30\x05\x82\x03" ".fr"                                                   \
    "\x30\x05\x82\x03" ".gp"                                                   \
    "\x30\x05\x82\x03" ".gf"                                                   \
    "\x30\x05\x82\x03" ".mq"                                                   \
    "\x30\x05\x82\x03" ".re"                                                   \
    "\x30\x05\x82\x03" ".yt"                                                   \
    "\x30\x05\x82\x03" ".pm"                                                   \
    "\x30\x05\x82\x03" ".bl"                                                   \
    "\x30\x05\x82\x03" ".mf"                                                   \
    "\x30\x05\x82\x03" ".wf"                                                   \
    "\x30\x05\x82\x03" ".pf"                                                   \
    "\x30\x05\x82\x03" ".nc"                                                   \
    "\x30\x05\x82\x03" ".tf"

/* TUBITAK Kamu SM SSL Kok Sertifikasi - Surum 1 */

#define TUBITAK1_SUBJECT_DN                                                    \
    "\x30\x81\xd2"                                                             \
    "\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02"                             \
    /* C */ "TR"                                                               \
    "\x31\x18\x30\x16\x06\x03\x55\x04\x07\x13\x0f"                             \
    /* L */ "Gebze - Kocaeli"                                                  \
    "\x31\x42\x30\x40\x06\x03\x55\x04\x0a\x13\x39"                             \
    /* O */ "Turkiye Bilimsel ve Teknolojik Arastirma Kurumu - TUBITAK"        \
    "\x31\x2d\x30\x2b\x06\x03\x55\x04\x0b\x13\x24"                             \
    /* OU */ "Kamu Sertifikasyon Merkezi - Kamu SM"                            \
    "\x31\x36\x30\x34\x06\x03\x55\x04\x03\x13\x2d"                             \
    /* CN */ "TUBITAK Kamu SM SSL Kok Sertifikasi - Surum 1"

#define TUBITAK1_NAME_CONSTRAINTS                                              \
    "\x30\x09\xa0\x07"                                                         \
    "\x30\x05\x82\x03" ".tr"

/* clang-format on */

static const SECItem builtInNameConstraints[][2] = {
    NAME_CONSTRAINTS_ENTRY(ANSSI),
    NAME_CONSTRAINTS_ENTRY(TUBITAK1)
};

SECStatus
CERT_GetImposedNameConstraints(const SECItem *derSubject, SECItem *extensions)
{
    size_t i;

    if (!extensions) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return SECFailure;
    }

    for (i = 0; i < PR_ARRAY_SIZE(builtInNameConstraints); ++i) {
        if (SECITEM_ItemsAreEqual(derSubject, &builtInNameConstraints[i][0])) {
            return SECITEM_CopyItem(NULL, extensions,
                                    &builtInNameConstraints[i][1]);
        }
    }

    PORT_SetError(SEC_ERROR_EXTENSION_NOT_FOUND);
    return SECFailure;
}

/*
 * Extract the name constraints extension from the CA cert.
 * If the certificate contains no name constraints extension, but
 * CERT_GetImposedNameConstraints returns a name constraints extension
 * for the subject of the certificate, then that extension will be returned.
 */
SECStatus
CERT_FindNameConstraintsExten(PLArenaPool *arena, CERTCertificate *cert,
                              CERTNameConstraints **constraints)
{
    SECStatus rv = SECSuccess;
    SECItem constraintsExtension;
    void *mark = NULL;

    *constraints = NULL;

    rv = CERT_FindCertExtension(cert, SEC_OID_X509_NAME_CONSTRAINTS,
                                &constraintsExtension);
    if (rv != SECSuccess) {
        if (PORT_GetError() != SEC_ERROR_EXTENSION_NOT_FOUND) {
            return rv;
        }
        rv = CERT_GetImposedNameConstraints(&cert->derSubject,
                                            &constraintsExtension);
        if (rv != SECSuccess) {
            if (PORT_GetError() == SEC_ERROR_EXTENSION_NOT_FOUND) {
                return SECSuccess;
            }
            return rv;
        }
    }

    mark = PORT_ArenaMark(arena);

    *constraints = cert_DecodeNameConstraints(arena, &constraintsExtension);
    if (*constraints == NULL) { /* decode failed */
        rv = SECFailure;
    }
    PORT_Free(constraintsExtension.data);

    if (rv == SECFailure) {
        PORT_ArenaRelease(arena, mark);
    } else {
        PORT_ArenaUnmark(arena, mark);
    }

    return rv;
}

/* Verify name against all the constraints relevant to that type of
** the name.
*/
SECStatus
CERT_CheckNameSpace(PLArenaPool *arena, const CERTNameConstraints *constraints,
                    const CERTGeneralName *currentName)
{
    CERTNameConstraint *matchingConstraints;
    SECStatus rv = SECSuccess;

    if (constraints->excluded != NULL) {
        rv = CERT_GetNameConstraintByType(constraints->excluded,
                                          currentName->type,
                                          &matchingConstraints, arena);
        if (rv == SECSuccess && matchingConstraints != NULL) {
            rv = cert_CompareNameWithConstraints(currentName,
                                                 matchingConstraints, PR_TRUE);
        }
        if (rv != SECSuccess) {
            return (rv);
        }
    }

    if (constraints->permited != NULL) {
        rv = CERT_GetNameConstraintByType(constraints->permited,
                                          currentName->type,
                                          &matchingConstraints, arena);
        if (rv == SECSuccess && matchingConstraints != NULL) {
            rv = cert_CompareNameWithConstraints(currentName,
                                                 matchingConstraints, PR_FALSE);
        }
        if (rv != SECSuccess) {
            return (rv);
        }
    }

    return (SECSuccess);
}

/* Extract the name constraints extension from the CA cert.
** Test each and every name in namesList against all the constraints
** relevant to that type of name.
** Returns NULL in pBadCert for success, if all names are acceptable.
** If some name is not acceptable, returns a pointer to the cert that
** contained that name.
*/
SECStatus
CERT_CompareNameSpace(CERTCertificate *cert, CERTGeneralName *namesList,
                      CERTCertificate **certsList, PLArenaPool *reqArena,
                      CERTCertificate **pBadCert)
{
    SECStatus rv = SECSuccess;
    CERTNameConstraints *constraints;
    CERTGeneralName *currentName;
    int count = 0;
    CERTCertificate *badCert = NULL;

    /* If no names to check, then no names can be bad. */
    if (!namesList)
        goto done;
    rv = CERT_FindNameConstraintsExten(reqArena, cert, &constraints);
    if (rv != SECSuccess) {
        count = -1;
        goto done;
    }

    currentName = namesList;
    do {
        if (constraints) {
            rv = CERT_CheckNameSpace(reqArena, constraints, currentName);
            if (rv != SECSuccess) {
                break;
            }
        }
        currentName = CERT_GetNextGeneralName(currentName);
        count++;
    } while (currentName != namesList);

done:
    if (rv != SECSuccess) {
        badCert = (count >= 0) ? certsList[count] : cert;
    }
    if (pBadCert)
        *pBadCert = badCert;

    return rv;
}

#if 0
/* not exported from shared libs, not used.  Turn on if we ever need it. */
SECStatus
CERT_CompareGeneralName(CERTGeneralName *a, CERTGeneralName *b)
{
    CERTGeneralName *currentA;
    CERTGeneralName *currentB;
    PRBool found;

    currentA = a;
    currentB = b;
    if (a != NULL) {
	do {
	    if (currentB == NULL) {
		return SECFailure;
	    }
	    currentB = CERT_GetNextGeneralName(currentB);
	    currentA = CERT_GetNextGeneralName(currentA);
	} while (currentA != a);
    }
    if (currentB != b) {
	return SECFailure;
    }
    currentA = a;
    do {
	currentB = b;
	found = PR_FALSE;
	do {
	    if (currentB->type == currentA->type) {
		switch (currentB->type) {
		  case certDNSName:
		  case certEDIPartyName:
		  case certIPAddress:
		  case certRegisterID:
		  case certRFC822Name:
		  case certX400Address:
		  case certURI:
		    if (SECITEM_CompareItem(&currentA->name.other,
					    &currentB->name.other)
			== SECEqual) {
			found = PR_TRUE;
		    }
		    break;
		  case certOtherName:
		    if (SECITEM_CompareItem(&currentA->name.OthName.oid,
					    &currentB->name.OthName.oid)
			== SECEqual &&
			SECITEM_CompareItem(&currentA->name.OthName.name,
					    &currentB->name.OthName.name)
			== SECEqual) {
			found = PR_TRUE;
		    }
		    break;
		  case certDirectoryName:
		    if (CERT_CompareName(&currentA->name.directoryName,
					 &currentB->name.directoryName)
			== SECEqual) {
			found = PR_TRUE;
		    }
		}

	    }
	    currentB = CERT_GetNextGeneralName(currentB);
	} while (currentB != b && found != PR_TRUE);
	if (found != PR_TRUE) {
	    return SECFailure;
	}
	currentA = CERT_GetNextGeneralName(currentA);
    } while (currentA != a);
    return SECSuccess;
}

SECStatus
CERT_CompareGeneralNameLists(CERTGeneralNameList *a, CERTGeneralNameList *b)
{
    SECStatus rv;

    if (a == b) {
	return SECSuccess;
    }
    if (a != NULL && b != NULL) {
	PR_Lock(a->lock);
	PR_Lock(b->lock);
	rv = CERT_CompareGeneralName(a->name, b->name);
	PR_Unlock(a->lock);
	PR_Unlock(b->lock);
    } else {
	rv = SECFailure;
    }
    return rv;
}
#endif

#if 0
/* This function is not exported from NSS shared libraries, and is not
** used inside of NSS.
** XXX it doesn't check for failed allocations. :-(
*/
void *
CERT_GetGeneralNameFromListByType(CERTGeneralNameList *list,
				  CERTGeneralNameType type,
				  PLArenaPool *arena)
{
    CERTName *name = NULL;
    SECItem *item = NULL;
    OtherName *other = NULL;
    OtherName *tmpOther = NULL;
    void *data;

    PR_Lock(list->lock);
    data = CERT_GetGeneralNameByType(list->name, type, PR_FALSE);
    if (data != NULL) {
	switch (type) {
	  case certDNSName:
	  case certEDIPartyName:
	  case certIPAddress:
	  case certRegisterID:
	  case certRFC822Name:
	  case certX400Address:
	  case certURI:
	    if (arena != NULL) {
		item = PORT_ArenaNew(arena, SECItem);
		if (item != NULL) {
XXX		    SECITEM_CopyItem(arena, item, (SECItem *) data);
		}
	    } else {
		item = SECITEM_DupItem((SECItem *) data);
	    }
	    PR_Unlock(list->lock);
	    return item;
	  case certOtherName:
	    other = (OtherName *) data;
	    if (arena != NULL) {
		tmpOther = PORT_ArenaNew(arena, OtherName);
	    } else {
		tmpOther = PORT_New(OtherName);
	    }
	    if (tmpOther != NULL) {
XXX		SECITEM_CopyItem(arena, &tmpOther->oid, &other->oid);
XXX		SECITEM_CopyItem(arena, &tmpOther->name, &other->name);
	    }
	    PR_Unlock(list->lock);
	    return tmpOther;
	  case certDirectoryName:
	    if (arena) {
		name = PORT_ArenaZNew(list->arena, CERTName);
		if (name) {
XXX		    CERT_CopyName(arena, name, (CERTName *) data);
		}
	    }
	    PR_Unlock(list->lock);
	    return name;
	}
    }
    PR_Unlock(list->lock);
    return NULL;
}
#endif

#if 0
/* This function is not exported from NSS shared libraries, and is not
** used inside of NSS.
** XXX it should NOT be a void function, since it does allocations
** that can fail.
*/
void
CERT_AddGeneralNameToList(CERTGeneralNameList *list,
			  CERTGeneralNameType type,
			  void *data, SECItem *oid)
{
    CERTGeneralName *name;

    if (list != NULL && data != NULL) {
	PR_Lock(list->lock);
	name = CERT_NewGeneralName(list->arena, type);
	if (!name)
	    goto done;
	switch (type) {
	  case certDNSName:
	  case certEDIPartyName:
	  case certIPAddress:
	  case certRegisterID:
	  case certRFC822Name:
	  case certX400Address:
	  case certURI:
XXX	    SECITEM_CopyItem(list->arena, &name->name.other, (SECItem *)data);
	    break;
	  case certOtherName:
XXX	    SECITEM_CopyItem(list->arena, &name->name.OthName.name,
			     (SECItem *) data);
XXX	    SECITEM_CopyItem(list->arena, &name->name.OthName.oid,
			     oid);
	    break;
	  case certDirectoryName:
XXX	    CERT_CopyName(list->arena, &name->name.directoryName,
			  (CERTName *) data);
	    break;
	}
	list->name = cert_CombineNamesLists(list->name, name);
	list->len++;
done:
	PR_Unlock(list->lock);
    }
    return;
}
#endif
