/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _FREEBL_H_
#define _FREEBL_H_

#include "seccomon.h"

#define X25519_PUBLIC_KEY_BYTES 32U
#define ED25519_PRIVATE_KEY_BYTES 32U
#define SECP256_PUBLIC_KEY_BYTES 65U
#define SECP384_PUBLIC_KEY_BYTES 97U

/* deprecated */
typedef enum {
    ECPoint_Uncompressed,
    ECPoint_XOnly,
    ECPoint_Undefined
} ECPointEncoding;

/* EC point compression format. blapit.h includes this header, so freebl and
 * its callers get these from here too. */
#define EC_POINT_FORM_COMPRESSED_Y0 0x02
#define EC_POINT_FORM_COMPRESSED_Y1 0x03
#define EC_POINT_FORM_UNCOMPRESSED 0x04
#define EC_POINT_FORM_HYBRID_Y0 0x06
#define EC_POINT_FORM_HYBRID_Y1 0x07

/*
 * Is this ECPoint the bare point, rather than the DER encoding of one?
 *
 * CKA_EC_POINT and the ECPoint in an ECPrivateKey are defined as the DER
 * encoding of an OCTET STRING, but plenty of tokens hand back the bare point
 * instead, so readers have to take either. The first byte doesn't tell them
 * apart: EC_POINT_FORM_UNCOMPRESSED and the OCTET STRING tag are both 0x04,
 * and a bare uncompressed point is itself a well formed OCTET STRING whenever
 * X's first byte happens to equal the point's length - 2 (63 for P-256, 95
 * for P-384). The lengths do tell them apart, so check those first.
 *
 * fieldLen is the size of one coordinate in bytes. Pass 0 if the curve isn't
 * known: the point is then reported as not bare, so a caller deciding whether
 * to unwrap falls back on trying the decode.
 */
static inline PRBool
ECPoint_IsBare(const SECItem *point, unsigned int fieldLen)
{
    if (fieldLen == 0 || point == NULL || point->data == NULL) {
        return PR_FALSE;
    }
    /* EC_POINT_FORM_UNCOMPRESSED || X || Y */
    if (point->len == 2 * fieldLen + 1 &&
        point->data[0] == EC_POINT_FORM_UNCOMPRESSED) {
        return PR_TRUE;
    }
    /* EC_POINT_FORM_COMPRESSED_Y0/Y1 || X */
    if (point->len == fieldLen + 1 &&
        (point->data[0] == EC_POINT_FORM_COMPRESSED_Y0 ||
         point->data[0] == EC_POINT_FORM_COMPRESSED_Y1)) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

#endif /* _FREEBL_H_ */
