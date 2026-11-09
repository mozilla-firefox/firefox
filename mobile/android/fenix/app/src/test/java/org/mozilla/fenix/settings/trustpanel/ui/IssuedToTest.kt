/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.settings.trustpanel.store.CertificateDetails

class IssuedToTest {

    @Test
    fun `GIVEN all subject fields WHEN mapped THEN all fields are shown with the country name`() {
        val certificateDetails =
            CertificateDetails(
                subjectOrganization = "The Guardian",
                subjectLocality = "London",
                subjectCountry = "GB",
                subjectCommonName = "www.theguardian.com",
            )

        assertEquals(
            IssuedTo(organization = "The Guardian", locality = "London", country = "United Kingdom"),
            certificateDetails.toIssuedTo(Locale.ENGLISH),
        )
    }

    @Test
    fun `GIVEN some subject fields are missing WHEN mapped THEN only the present fields are shown`() {
        val certificateDetails = CertificateDetails(subjectOrganization = "Mozilla Corporation", subjectLocality = " ")

        assertEquals(
            IssuedTo(organization = "Mozilla Corporation", locality = null, country = null),
            certificateDetails.toIssuedTo(Locale.ENGLISH),
        )
    }

    @Test
    fun `GIVEN all subject fields are missing WHEN mapped THEN nothing is shown`() {
        assertNull(CertificateDetails(subjectCommonName = "example.com").toIssuedTo(Locale.ENGLISH))
    }

    @Test
    fun `GIVEN no certificate WHEN mapped THEN nothing is shown`() {
        assertNull((null as CertificateDetails?).toIssuedTo(Locale.ENGLISH))
    }

    @Test
    fun `WHEN a country code is converted THEN its name in the given locale is returned`() {
        assertEquals("United Kingdom", countryDisplayName("GB", Locale.ENGLISH))
        assertEquals("Royaume-Uni", countryDisplayName("GB", Locale.FRENCH))
        assertEquals("Germany", countryDisplayName("de", Locale.ENGLISH))
    }

    @Test
    fun `WHEN an invalid country code is converted THEN the code is returned`() {
        assertEquals("Not a country", countryDisplayName("Not a country", Locale.ENGLISH))
    }
}
