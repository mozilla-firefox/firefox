/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.settings.trustpanel.store.CertificateDetails

class IssuerDisplayNameTest {

    @Test
    fun `GIVEN an issuer organization and common name WHEN getting the issuer name THEN the organization is used`() {
        val certificateDetails =
            CertificateDetails(
                issuerOrganization = "GlobalSign nv-sa",
                issuerCommonName = "GlobalSign GCC R3 OV TLS CA",
            )

        assertEquals("GlobalSign nv-sa", certificateDetails.issuerDisplayName())
    }

    @Test
    fun `GIVEN only an issuer common name WHEN getting the issuer name THEN the common name is used`() {
        assertEquals("R13", CertificateDetails(issuerCommonName = "R13").issuerDisplayName())
    }

    @Test
    fun `GIVEN a blank issuer organization WHEN getting the issuer name THEN the common name is used`() {
        assertEquals("R13", CertificateDetails(issuerOrganization = " ", issuerCommonName = "R13").issuerDisplayName())
    }

    @Test
    fun `GIVEN neither an issuer organization nor a common name WHEN getting the issuer name THEN it is null`() {
        assertNull(CertificateDetails(subjectOrganization = "Mozilla Corporation").issuerDisplayName())
    }

    @Test
    fun `GIVEN no certificate details WHEN getting the issuer name THEN it is null`() {
        assertNull((null as CertificateDetails?).issuerDisplayName())
    }
}
