/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* libssl functions implemented with mozpkix. */

#include "ssl.h"
#include "sslimpl.h"

#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"

using namespace mozilla::pkix;

/* Parse a DER-encoded certificate with mozpkix and return its
 * subjectPublicKeyInfo.
 *
 * This rejects certificates over 65,535 bytes and
 * those with unknown critical extensions.
 *
 * Free the result with SECKEY_DestroySubjectPublicKeyInfo().
 */
CERTSubjectPublicKeyInfo *
ssl_ExtractSPKIFromDER(const SECItem *derCert)
{
    if (!derCert || !derCert->data || !derCert->len) {
        PORT_SetError(SEC_ERROR_INVALID_ARGS);
        return NULL;
    }

    Input certDER;
    Result rv = certDER.Init(derCert->data, derCert->len);
    if (rv != Success) {
        PORT_SetError(MapResultToPRErrorCode(rv));
        return NULL;
    }

    Input spkiDER;
    rv = FindSubjectPublicKeyInfo(certDER, spkiDER);
    if (rv != Success) {
        PORT_SetError(MapResultToPRErrorCode(rv));
        return NULL;
    }

    SECItem spkiItem = UnsafeMapInputToSECItem(spkiDER);
    return SECKEY_DecodeDERSubjectPublicKeyInfo(&spkiItem);
}
