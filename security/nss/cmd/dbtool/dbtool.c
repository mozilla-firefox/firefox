/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
** dbtool.c
**
** tool to dump the underlying encoding of a database. This tool duplicates
**  some private functions in softoken. It uses libsec and libutil, but no
**  other portions of NSS. It currently only works on sqlite databases. For
**  an even more primitive dump, use sqlite3 on the individual files.
**
**   TODO: dump the meta data for the databases.
**         optionally dump more PKCS5 information (KDF/salt/iterations)
**         take a password and decode encrypted attributes/verify signed
**             attributes.
*/
#include <stdio.h>
#include <string.h>

#if defined(WIN32)
#include "fcntl.h"
#include "io.h"
#endif

/*#include "secutil.h" */
/*#include "pk11pub.h" */

#if defined(XP_UNIX)
#include <unistd.h>
#endif

#include "nspr.h"
#include "prtypes.h"
#include "nss.h"
#include "secasn1.h"
#include "secder.h"
#include "pk11table.h"
#include "sftkdbt.h"
#include "sdb.h"
#include "secoid.h"

#include "plgetopt.h"

static char *progName;

char *dbDir = NULL;

static void
Usage()
{
    printf("Usage:  %s [-c certprefix] [-k keyprefix] "
           "[-V certversion] [-v keyversion]\n"
           "           [-d dbdir]\n",
           progName);
    printf("%-20s Directory with cert database (default is .)\n",
           "-d certdir");
    printf("%-20s prefix for the cert database (default is \"\")\n",
           "-c certprefix");
    printf("%-20s prefix for the key database (default is \"\")\n",
           "-k keyprefix");
    printf("%-20s version of the cert database (default is 9)\n",
           "-V certversion");
    printf("%-20s version of the key database (default is 4)\n",
           "-v keyversion");
    exit(1);
}
#define SFTK_KEYDB_TYPE 0x40000000
#define SFTK_TOKEN_TYPE 0x80000000

/*
 * known attributes
 */
static const CK_ATTRIBUTE_TYPE known_attributes[] = {
    CKA_CLASS, CKA_TOKEN, CKA_PRIVATE, CKA_LABEL, CKA_APPLICATION,
    CKA_VALUE, CKA_OBJECT_ID, CKA_CERTIFICATE_TYPE, CKA_ISSUER,
    CKA_SERIAL_NUMBER, CKA_AC_ISSUER, CKA_OWNER, CKA_ATTR_TYPES, CKA_TRUSTED,
    CKA_CERTIFICATE_CATEGORY, CKA_JAVA_MIDP_SECURITY_DOMAIN, CKA_URL,
    CKA_HASH_OF_SUBJECT_PUBLIC_KEY, CKA_HASH_OF_ISSUER_PUBLIC_KEY,
    CKA_CHECK_VALUE, CKA_KEY_TYPE, CKA_SUBJECT, CKA_ID, CKA_SENSITIVE,
    CKA_ENCRYPT, CKA_DECRYPT, CKA_WRAP, CKA_UNWRAP, CKA_SIGN, CKA_SIGN_RECOVER,
    CKA_VERIFY, CKA_VERIFY_RECOVER, CKA_DERIVE, CKA_START_DATE, CKA_END_DATE,
    CKA_MODULUS, CKA_MODULUS_BITS, CKA_PUBLIC_EXPONENT, CKA_PRIVATE_EXPONENT,
    CKA_PRIME_1, CKA_PRIME_2, CKA_EXPONENT_1, CKA_EXPONENT_2, CKA_COEFFICIENT,
    CKA_PRIME, CKA_SUBPRIME, CKA_BASE, CKA_PRIME_BITS,
    CKA_SUB_PRIME_BITS, CKA_VALUE_BITS, CKA_VALUE_LEN, CKA_EXTRACTABLE,
    CKA_LOCAL, CKA_NEVER_EXTRACTABLE, CKA_ALWAYS_SENSITIVE,
    CKA_KEY_GEN_MECHANISM, CKA_MODIFIABLE, CKA_EC_PARAMS,
    CKA_EC_POINT, CKA_SECONDARY_AUTH, CKA_AUTH_PIN_FLAGS,
    CKA_ALWAYS_AUTHENTICATE, CKA_WRAP_WITH_TRUSTED, CKA_WRAP_TEMPLATE,
    CKA_UNWRAP_TEMPLATE, CKA_HW_FEATURE_TYPE, CKA_RESET_ON_INIT,
    CKA_HAS_RESET, CKA_PIXEL_X, CKA_PIXEL_Y, CKA_RESOLUTION, CKA_CHAR_ROWS,
    CKA_CHAR_COLUMNS, CKA_COLOR, CKA_BITS_PER_PIXEL, CKA_CHAR_SETS,
    CKA_ENCODING_METHODS, CKA_MIME_TYPES, CKA_MECHANISM_TYPE,
    CKA_REQUIRED_CMS_ATTRIBUTES, CKA_DEFAULT_CMS_ATTRIBUTES,
    CKA_SUPPORTED_CMS_ATTRIBUTES, CKA_NSS_URL, CKA_NSS_EMAIL,
    CKA_NSS_SMIME_INFO, CKA_NSS_SMIME_TIMESTAMP,
    CKA_NSS_PKCS8_SALT, CKA_NSS_PASSWORD_CHECK, CKA_NSS_EXPIRES,
    CKA_NSS_KRL, CKA_NSS_PQG_COUNTER, CKA_NSS_PQG_SEED,
    CKA_NSS_PQG_H, CKA_NSS_PQG_SEED_BITS, CKA_NSS_MODULE_SPEC,
    CKA_TRUST_DIGITAL_SIGNATURE, CKA_TRUST_NON_REPUDIATION,
    CKA_TRUST_KEY_ENCIPHERMENT, CKA_TRUST_DATA_ENCIPHERMENT,
    CKA_TRUST_KEY_AGREEMENT, CKA_TRUST_KEY_CERT_SIGN, CKA_TRUST_CRL_SIGN,
    CKA_TRUST_SERVER_AUTH, CKA_TRUST_CLIENT_AUTH, CKA_TRUST_CODE_SIGNING,
    CKA_TRUST_EMAIL_PROTECTION, CKA_TRUST_IPSEC_END_SYSTEM,
    CKA_TRUST_IPSEC_TUNNEL, CKA_TRUST_IPSEC_USER, CKA_TRUST_TIME_STAMPING,
    CKA_TRUST_STEP_UP_APPROVED, CKA_CERT_SHA1_HASH, CKA_CERT_MD5_HASH,
    CKA_NSS_DB, CKA_NSS_TRUST, CKA_NSS_OVERRIDE_EXTENSIONS,
    CKA_PUBLIC_KEY_INFO
};

static unsigned int known_attributes_size = sizeof(known_attributes) /
                                            sizeof(known_attributes[0]);

PRBool
isULONGAttribute(CK_ATTRIBUTE_TYPE type)
{
    switch (type) {
        case CKA_CERTIFICATE_CATEGORY:
        case CKA_CERTIFICATE_TYPE:
        case CKA_CLASS:
        case CKA_JAVA_MIDP_SECURITY_DOMAIN:
        case CKA_KEY_GEN_MECHANISM:
        case CKA_KEY_TYPE:
        case CKA_MECHANISM_TYPE:
        case CKA_MODULUS_BITS:
        case CKA_PRIME_BITS:
        case CKA_SUBPRIME_BITS:
        case CKA_VALUE_BITS:
        case CKA_VALUE_LEN:

        case CKA_TRUST_DIGITAL_SIGNATURE:
        case CKA_TRUST_NON_REPUDIATION:
        case CKA_TRUST_KEY_ENCIPHERMENT:
        case CKA_TRUST_DATA_ENCIPHERMENT:
        case CKA_TRUST_KEY_AGREEMENT:
        case CKA_TRUST_KEY_CERT_SIGN:
        case CKA_TRUST_CRL_SIGN:

        case CKA_TRUST_SERVER_AUTH:
        case CKA_TRUST_CLIENT_AUTH:
        case CKA_TRUST_CODE_SIGNING:
        case CKA_TRUST_EMAIL_PROTECTION:
        case CKA_TRUST_IPSEC_END_SYSTEM:
        case CKA_TRUST_IPSEC_TUNNEL:
        case CKA_TRUST_IPSEC_USER:
        case CKA_TRUST_TIME_STAMPING:
        case CKA_TRUST_STEP_UP_APPROVED:
            return PR_TRUE;
        default:
            break;
    }
    return PR_FALSE;
}

/* are the attributes private? */
static PRBool
isPrivateAttribute(CK_ATTRIBUTE_TYPE type)
{
    switch (type) {
        case CKA_VALUE:
        case CKA_PRIVATE_EXPONENT:
        case CKA_PRIME_1:
        case CKA_PRIME_2:
        case CKA_EXPONENT_1:
        case CKA_EXPONENT_2:
        case CKA_COEFFICIENT:
            return PR_TRUE;
        default:
            break;
    }
    return PR_FALSE;
}

/* These attributes must be authenticated with an hmac. */
static PRBool
isAuthenticatedAttribute(CK_ATTRIBUTE_TYPE type)
{
    switch (type) {
        case CKA_MODULUS:
        case CKA_PUBLIC_EXPONENT:
        case CKA_CERT_SHA1_HASH:
        case CKA_CERT_MD5_HASH:
        case CKA_TRUST_SERVER_AUTH:
        case CKA_TRUST_CLIENT_AUTH:
        case CKA_TRUST_EMAIL_PROTECTION:
        case CKA_TRUST_CODE_SIGNING:
        case CKA_TRUST_STEP_UP_APPROVED:
        case CKA_NSS_OVERRIDE_EXTENSIONS:
            return PR_TRUE;
        default:
            break;
    }
    return PR_FALSE;
}

/*
 * convert a database ulong back to a native ULONG. (reverse of the above
 * function.
 */
static CK_ULONG
sdbULong2ULong(unsigned char *data)
{
    int i;
    CK_ULONG value = 0;

    for (i = 0; i < SDB_ULONG_SIZE; i++) {
        value |= (((CK_ULONG)data[i]) << (SDB_ULONG_SIZE - 1 - i) * PR_BITS_PER_BYTE);
    }
    return value;
}

/* PBE defines and functions */
SEC_ASN1_MKSUB(SECOID_AlgorithmIDTemplate)

typedef struct EncryptedDataInfoStr {
    SECAlgorithmID algorithm;
    SECItem encryptedData;
} EncryptedDataInfo;

static const SEC_ASN1Template encryptedDataInfoTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(EncryptedDataInfo) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(EncryptedDataInfo, algorithm),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(EncryptedDataInfo, encryptedData) },
    { 0 }
};

typedef struct PBEParameterStr {
    SECAlgorithmID prfAlg;
    SECItem salt;
    SECItem iteration;
    SECItem keyLength;
} PBEParameter;

static const SEC_ASN1Template pkcs5V1PBEParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE,
      0, NULL, sizeof(PBEParameter) },
    { SEC_ASN1_OCTET_STRING,
      offsetof(PBEParameter, salt) },
    { SEC_ASN1_INTEGER,
      offsetof(PBEParameter, iteration) },
    { 0 }
};

static const SEC_ASN1Template pkcs12V2PBEParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(PBEParameter) },
    { SEC_ASN1_OCTET_STRING, offsetof(PBEParameter, salt) },
    { SEC_ASN1_INTEGER, offsetof(PBEParameter, iteration) },
    { 0 }
};

static const SEC_ASN1Template pkcs5V2PBEParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(PBEParameter) },
    /* this is really a choice, but since we don't understand any other
     * choice, just inline it. */
    { SEC_ASN1_OCTET_STRING, offsetof(PBEParameter, salt) },
    { SEC_ASN1_INTEGER, offsetof(PBEParameter, iteration) },
    { SEC_ASN1_INTEGER, offsetof(PBEParameter, keyLength) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(PBEParameter, prfAlg),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { 0 }
};

typedef struct Pkcs5v2PBEParameterStr {
    SECAlgorithmID keyParams; /* parameters of the key generation */
    SECAlgorithmID algParams; /* parameters for the encryption or mac op */
} Pkcs5v2PBEParameter;

static const SEC_ASN1Template pkcs5v2PBES2ParameterTemplate[] = {
    { SEC_ASN1_SEQUENCE, 0, NULL, sizeof(Pkcs5v2PBEParameter) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(Pkcs5v2PBEParameter, keyParams),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { SEC_ASN1_INLINE | SEC_ASN1_XTRN,
      offsetof(Pkcs5v2PBEParameter, algParams),
      SEC_ASN1_SUB(SECOID_AlgorithmIDTemplate) },
    { 0 }
};

static inline PRBool
isPKCS12PBE(SECOidTag alg)
{
    switch (alg) {
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_2KEY_TRIPLE_DES_CBC:
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_3KEY_TRIPLE_DES_CBC:
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_128_BIT_RC2_CBC:
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_40_BIT_RC2_CBC:
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_128_BIT_RC4:
        case SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_40_BIT_RC4:
            return PR_TRUE;
        default:
            break;
    }
    return PR_FALSE;
}

/* helper functions */

/* output an NSS specific attribute or name that wasn't found in our
 * pkcs #11 table */
const char *
makeNSSVendorName(CK_ATTRIBUTE_TYPE attribute, const char *nameType)
{
    static char nss_name[256];
    const char *name = NULL;
    if ((attribute >= CKA_NSS) && (attribute < 0xffffffffUL)) {
        snprintf(nss_name, sizeof(nss_name), "%s+%d", nameType, (int)(attribute - CKA_NSS));
        name = nss_name;
    }
    return name;
}

/*  turn and attribute into a name */
const char *
AttributeName(CK_ATTRIBUTE_TYPE attribute)
{
    const char *name = getNameFromAttribute(attribute);
    if (!name) {
        name = makeNSSVendorName(attribute, "CKA_NSS");
    }

    return name ? name : "UNKNOWN_ATTRIBUTE_TYPE";
}

/*  turn and error code into a name */
const char *
ErrorName(CK_RV crv)
{
    const char *error = getName(crv, ConstResult);
    if (!error) {
        error = makeNSSVendorName(crv, "CKR_NSS");
    }
    return error ? error : "UNKNOWN_ERROR";
}

/* turn an oud tag into a string */
const char *
oid2string(SECOidTag alg)
{
    const char *oidstring = SECOID_FindOIDTagDescription(alg);
    const char *def = "Invalid oid tag"; /* future build a dotted oid string value here */
    return oidstring ? oidstring : def;
}

/* dump an arbitary data blob. Dump it has hex with ascii on the side */
#define ASCCHAR(val) ((val) >= ' ' && (val) <= 0x7e ? (val) : '.')
#define LINE_LENGTH 16
void
dumpValue(const unsigned char *v, int len)
{
    int i, next = 0;
    char string[LINE_LENGTH + 1];
    char space[LINE_LENGTH * 2 + 1];
    char *nl = "";
    char *sp = "";
    PORT_Memset(string, 0, sizeof(string));

    for (i = 0; i < len; i++) {
        if ((i % LINE_LENGTH) == 0) {
            printf("%s%s%s        ", sp, string, nl);
            PORT_Memset(string, 0, sizeof(string));
            next = 0;
            nl = "\n";
            sp = " ";
        }
        printf("%02x", v[i]);
        string[next++] = ASCCHAR(v[i]);
    }
    PORT_Memset(space, 0, sizeof(space));
    i = LINE_LENGTH - (len % LINE_LENGTH);
    if (i != LINE_LENGTH) {
        int j;
        for (j = 0; j < i; j++) {
            space[j * 2] = ' ';
            space[j * 2 + 1] = ' ';
        }
    }
    printf("%s%s%s%s", space, sp, string, nl);
}

/* dump a PKCS5/12 PBE blob */
void
dumpPKCS(unsigned char *val, CK_ULONG len, PRBool *hasSig)
{
    EncryptedDataInfo edi;
    SECStatus rv;
    SECItem data;
    PLArenaPool *arena;
    SECOidTag alg, prfAlg;
    PBEParameter pbeParam;
    unsigned char zero = 0;
    const SEC_ASN1Template *template = pkcs5V1PBEParameterTemplate;
    int iter, keyLen, i;

    if (hasSig) {
        *hasSig = PR_FALSE;
    }

    data.data = val;
    data.len = len;
    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    if (arena == NULL) {
        printf("Couldn't allocate arena\n");
        return;
    }

    /* initialize default values */
    PORT_Memset(&pbeParam, 0, sizeof(pbeParam));
    pbeParam.keyLength.data = &zero;
    pbeParam.keyLength.len = sizeof(zero);
    SECOID_SetAlgorithmID(arena, &pbeParam.prfAlg, SEC_OID_SHA1, NULL);

    /* first crack the encrypted data from the PBE algorithm ID */
    rv = SEC_QuickDERDecodeItem(arena, &edi, encryptedDataInfoTemplate, &data);
    if (rv != SECSuccess) {
        printf("Encrypted Data, failed to decode\n");
        dumpValue(val, len);
        PORT_FreeArena(arena, PR_FALSE);
        return;
    }
    /* now use the pbe secalg to dump info on the pbe */
    alg = SECOID_GetAlgorithmTag(&edi.algorithm);
    if ((alg == SEC_OID_PKCS5_PBES2) || (alg == SEC_OID_PKCS5_PBMAC1)) {
        Pkcs5v2PBEParameter param;
        SECOidTag palg;
        const char *typeName = (alg == SEC_OID_PKCS5_PBES2) ? "Encrypted Data PBES2" : "Mac Data PBMAC1";

        rv = SEC_QuickDERDecodeItem(arena, &param,
                                    pkcs5v2PBES2ParameterTemplate,
                                    &edi.algorithm.parameters);
        if (rv != SECSuccess) {
            printf("%s, failed to decode\n", typeName);
            dumpValue(val, len);
            PORT_FreeArena(arena, PR_FALSE);
            return;
        }
        palg = SECOID_GetAlgorithmTag(&param.algParams);
        printf("%s alg=%s ", typeName, oid2string(palg));
        if (hasSig && palg == SEC_OID_AES_256_CBC) {
            *hasSig = PR_TRUE;
        }
        template = pkcs5V2PBEParameterTemplate;
        edi.algorithm.parameters = param.keyParams.parameters;
    } else {
        printf("Encrypted Data alg=%s ", oid2string(alg));
        if (alg == SEC_OID_PKCS5_PBKDF2) {
            template = pkcs5V2PBEParameterTemplate;
        } else if (isPKCS12PBE(alg)) {
            template = pkcs12V2PBEParameterTemplate;
        } else {
            template = pkcs5V1PBEParameterTemplate;
        }
    }
    rv = SEC_QuickDERDecodeItem(arena, &pbeParam,
                                template,
                                &edi.algorithm.parameters);
    if (rv != SECSuccess) {
        printf("( failed to decode params)\n");
        PORT_FreeArena(arena, PR_FALSE);
        return;
    }
    /* dump the pbe parmeters */
    iter = DER_GetInteger(&pbeParam.iteration);
    keyLen = DER_GetInteger(&pbeParam.keyLength);
    prfAlg = SECOID_GetAlgorithmTag(&pbeParam.prfAlg);
    printf("(prf=%s iter=%d keyLen=%d salt=0x",
           oid2string(prfAlg), iter, keyLen);
    for (i = 0; i < pbeParam.salt.len; i++)
        printf("%02x", pbeParam.salt.data[i]);
    printf(")\n");
    /* finally dump the raw encrypted data */
    dumpValue(edi.encryptedData.data, edi.encryptedData.len);
    PORT_FreeArena(arena, PR_FALSE);
}

/* dump a long attribute, convert to an unsigned long. PKCS #11 Longs are
 * limited to 32 bits by the spec, even if the CK_ULONG is longer */
void
dumpLongAttribute(CK_ATTRIBUTE_TYPE type, CK_ULONG value)
{
    const char *nameType = "CK_NSS";
    ConstType constType = ConstNone;
    const char *valueName = NULL;

    switch (type) {
        case CKA_CLASS:
            nameType = "CKO_NSS";
            constType = ConstObject;
            break;
        case CKA_CERTIFICATE_TYPE:
            nameType = "CKC_NSS";
            constType = ConstCertType;
            break;
        case CKA_KEY_TYPE:
            nameType = "CKK_NSS";
            constType = ConstKeyType;
            break;
        case CKA_MECHANISM_TYPE:
            nameType = "CKM_NSS";
            constType = ConstMechanism;
            break;
        case CKA_TRUST_SERVER_AUTH:
        case CKA_TRUST_CLIENT_AUTH:
        case CKA_TRUST_CODE_SIGNING:
        case CKA_TRUST_EMAIL_PROTECTION:
        case CKA_TRUST_IPSEC_END_SYSTEM:
        case CKA_TRUST_IPSEC_TUNNEL:
        case CKA_TRUST_IPSEC_USER:
        case CKA_TRUST_TIME_STAMPING:
            nameType = "CKT_NSS";
            constType = ConstTrust;
            break;
        default:
            break;
    }
    /* if value has a symbolic name, use it */
    if (constType != ConstNone) {
        valueName = getName(value, constType);
    }
    if (!valueName) {
        valueName = makeNSSVendorName(value, nameType);
    }
    if (!valueName) {
        printf("%d (0x%08x)\n", (int)value, (int)value);
    } else {
        printf("%s (0x%08x)\n", valueName, (int)value);
    }
}

/* dump a signature for an object */
static const char META_SIG_TEMPLATE[] = "sig_%s_%08x_%08x";
void
dumpSignature(CK_ATTRIBUTE_TYPE attribute, SDB *keydb, PRBool isKey,
              CK_OBJECT_HANDLE objectID, PRBool force)
{
    char id[30];
    CK_RV crv;
    SECItem signText;
    unsigned char signData[SDB_MAX_META_DATA_LEN];

    if (!force && !isAuthenticatedAttribute(attribute)) {
        return;
    }
    snprintf(id, sizeof(id), META_SIG_TEMPLATE,
            isKey ? "key" : "cert",
            (unsigned int)objectID, (unsigned int)attribute);
    printf("        Signature %s:", id);
    signText.data = signData;
    signText.len = sizeof(signData);

    crv = (*keydb->sdb_GetMetaData)(keydb, id, &signText, NULL);
    if ((crv != CKR_OK) && isKey) {
        snprintf(id, sizeof(id), META_SIG_TEMPLATE,
                isKey ? "key" : "cert", (unsigned int)(objectID | SFTK_KEYDB_TYPE | SFTK_TOKEN_TYPE),
                (unsigned int)attribute);
        crv = (*keydb->sdb_GetMetaData)(keydb, id, &signText, NULL);
    }
    if (crv != CKR_OK) {
        printf(" FAILED %s with %s (0x%08x)\n", id, ErrorName(crv), (int)crv);
        return;
    }
    dumpPKCS(signText.data, signText.len, NULL);
    return;
}

/* dump an attribute. use the helper functions above */
void
dumpAttribute(CK_ATTRIBUTE *template, SDB *keydb, PRBool isKey,
              CK_OBJECT_HANDLE id)
{
    CK_ATTRIBUTE_TYPE attribute = template->type;
    printf("      %s(0x%08x): ", AttributeName(attribute), (int)attribute);
    if (template->pValue == NULL) {
        printf("NULL (%d)\n", (int)template->ulValueLen);
        return;
    }
    if (template->ulValueLen == SDB_ULONG_SIZE && isULONGAttribute(attribute)) {
        CK_ULONG value = sdbULong2ULong(template->pValue);
        dumpLongAttribute(attribute, value);
        return;
    }
    if (template->ulValueLen == 1) {
        unsigned char val = *(unsigned char *)template->pValue;
        switch (val) {
            case 0:
                printf("CK_FALSE\n");
                break;
            case 1:
                printf("CK_TRUE\n");
                break;
            default:
                printf("%d 0x%02x %c\n", val, val, ASCCHAR(val));
                break;
        }
        return;
    }
    if (isKey && isPrivateAttribute(attribute)) {
        PRBool hasSig = PR_FALSE;
        dumpPKCS(template->pValue, template->ulValueLen, &hasSig);
        if (hasSig) {
            dumpSignature(attribute, keydb, isKey, id, PR_TRUE);
        }
        return;
    }
    if (template->ulValueLen == 0) {
        printf("empty");
    }
    printf("\n");
    dumpValue(template->pValue, template->ulValueLen);
}

/* dump all the attributes in an object */
void
dumpObject(CK_OBJECT_HANDLE id, SDB *db, SDB *keydb, PRBool isKey)
{
    CK_RV crv;
    int i;
    CK_ATTRIBUTE template;
    char buffer[2048];
    char *alloc = NULL;

    printf("  Object 0x%08x:\n", (int)id);
    for (i = 0; i < known_attributes_size; i++) {
        CK_ATTRIBUTE_TYPE attribute = known_attributes[i];
        template.type = attribute;
        template.pValue = NULL;
        template.ulValueLen = 0;
        crv = (*db->sdb_GetAttributeValue)(db, id, &template, 1);

        if (crv != CKR_OK) {
            if (crv != CKR_ATTRIBUTE_TYPE_INVALID) {
                PR_fprintf(PR_STDERR, "    "
                                      "Get Attribute %s (0x%08x):FAILED\"%s\"(0x%08x)\n",
                           AttributeName(attribute), (int)attribute,
                           ErrorName(crv), (int)crv);
            }
            continue;
        }

        if (template.ulValueLen < sizeof(buffer)) {
            template.pValue = buffer;
        } else {
            alloc = PORT_Alloc(template.ulValueLen);
            template.pValue = alloc;
        }
        if (template.pValue == NULL) {
            PR_fprintf(PR_STDERR, "    "
                                  "Could allocate %d bytes for  Attribute %s (0x%08x)\n",
                       (int)template.ulValueLen,
                       AttributeName(attribute), (int)attribute);
            continue;
        }
        crv = (*db->sdb_GetAttributeValue)(db, id, &template, 1);

        if (crv != CKR_OK) {
            if (crv != CKR_ATTRIBUTE_TYPE_INVALID) {
                PR_fprintf(PR_STDERR, "    "
                                      "Get Attribute %s (0x%08x):FAILED\"%s\"(0x%08x)\n",
                           AttributeName(attribute), (int)attribute,
                           ErrorName(crv), (int)crv);
            }
            if (alloc) {
                PORT_Free(alloc);
                alloc = NULL;
            }
            continue;
        }

        dumpAttribute(&template, keydb, isKey, id);
        dumpSignature(template.type, keydb, isKey, id, PR_FALSE);
        if (alloc) {
            PORT_Free(alloc);
            alloc = NULL;
        }
    }
}

/* dump all the objects in a database */
void
dumpDB(SDB *db, const char *name, SDB *keydb, PRBool isKey)
{
    SDBFind *findHandle = NULL;
    CK_BBOOL isTrue = 1;
    CK_ATTRIBUTE allObjectTemplate = { CKA_TOKEN, NULL, 1 };
    CK_ULONG allObjectTemplateCount = 1;
    PRBool recordFound = PR_FALSE;
    CK_RV crv = CKR_OK;
    CK_ULONG objectCount = 0;
    printf("%s:\n", name);

    allObjectTemplate.pValue = &isTrue;
    crv = (*db->sdb_FindObjectsInit)(db, &allObjectTemplate,
                                     allObjectTemplateCount, &findHandle);
    do {
        CK_OBJECT_HANDLE id;
        recordFound = PR_FALSE;
        crv = (*db->sdb_FindObjects)(db, findHandle, &id, 1, &objectCount);
        if ((crv == CKR_OK) && (objectCount == 1)) {
            recordFound = PR_TRUE;
            dumpObject(id, db, keydb, isKey);
        }
    } while (recordFound);
    if (crv != CKR_OK) {
        PR_fprintf(PR_STDERR,
                   "Last record return PKCS #11 error = %s (0x%08x)\n",
                   ErrorName(crv), (int)crv);
    }
    (*db->sdb_FindObjectsFinal)(db, findHandle);
}

static char *
secu_ConfigDirectory(const char *base)
{
    static PRBool initted = PR_FALSE;
    const char *dir = ".netscape";
    char *home;
    static char buf[1000];

    if (initted)
        return buf;

    if (base == NULL || *base == 0) {
        home = PR_GetEnvSecure("HOME");
        if (!home)
            home = "";

        if (*home && home[strlen(home) - 1] == '/')
            snprintf(buf, sizeof(buf), "%.900s%s", home, dir);
        else
            snprintf(buf, sizeof(buf), "%.900s/%s", home, dir);
    } else {
        snprintf(buf, sizeof(buf), "%.900s", base);
        if (buf[strlen(buf) - 1] == '/')
            buf[strlen(buf) - 1] = 0;
    }

    initted = PR_TRUE;
    return buf;
}

int
main(int argc, char **argv)
{
    PLOptState *optstate;
    PLOptStatus optstatus;
    char *certPrefix = "", *keyPrefix = "";
    int cert_version = 9;
    int key_version = 4;
    SDB *certdb = NULL;
    SDB *keydb = NULL;
    PRBool isNew = PR_FALSE;

    CK_RV crv;

    progName = strrchr(argv[0], '/');
    if (!progName)
        progName = strrchr(argv[0], '\\');
    progName = progName ? progName + 1 : argv[0];

    optstate = PL_CreateOptState(argc, argv, "d:c:k:v:V:h");

    while ((optstatus = PL_GetNextOpt(optstate)) == PL_OPT_OK) {
        switch (optstate->option) {
            case 'h':
            default:
                Usage();
                break;

            case 'd':
                dbDir = PORT_Strdup(optstate->value);
                break;

            case 'c':
                certPrefix = PORT_Strdup(optstate->value);
                break;

            case 'k':
                keyPrefix = PORT_Strdup(optstate->value);
                break;

            case 'v':
                key_version = atoi(optstate->value);
                break;

            case 'V':
                cert_version = atoi(optstate->value);
                break;
        }
    }
    PL_DestroyOptState(optstate);
    if (optstatus == PL_OPT_BAD)
        Usage();

    if (dbDir) {
        char *tmp = dbDir;
        dbDir = secu_ConfigDirectory(tmp);
        PORT_Free(tmp);
    } else {
        dbDir = secu_ConfigDirectory(NULL);
    }
    PR_fprintf(PR_STDERR, "dbdir selected is %s\n\n", dbDir);

    if (dbDir[0] == '\0') {
        PR_fprintf(PR_STDERR,
                   "ERROR: Directory \"%s\" does not exist.\n", dbDir);
        return 1;
    }

    PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);
    SECOID_Init();

    crv = s_open(dbDir, certPrefix, keyPrefix, cert_version, key_version,
                 SDB_RDONLY, &certdb, &keydb, &isNew);
    if (crv != CKR_OK) {
        PR_fprintf(PR_STDERR,
                   "Couldn't open databased in %s, error=%s (0x%08x)\n",
                   dbDir, ErrorName(crv), (int)crv);
        return 1;
    }

    /* now dump the objects in the cert database */
    dumpDB(certdb, "CertDB", keydb, PR_FALSE);
    dumpDB(keydb, "KeyDB", keydb, PR_TRUE);
    return 0;
}
