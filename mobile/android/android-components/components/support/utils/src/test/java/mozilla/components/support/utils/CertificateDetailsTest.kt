/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import java.security.cert.X509Certificate
import java.util.Locale
import javax.security.auth.x500.X500Principal
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mockito.Mockito

class CertificateDetailsTest {

    @Test
    fun `GIVEN a certificate with all fields WHEN mapped THEN all certificate details are present`() {
        val certificate =
            createCertificate(
                subject = "CN=www.theguardian.com, O=Guardian News & Media Ltd, L=London, C=GB",
                issuer = "CN=GlobalSign GCC R3 OV TLS CA 2024, O=GlobalSign nv-sa, C=BE",
            )

        assertEquals(
            CertificateDetails(
                subjectOrganization = "Guardian News & Media Ltd",
                subjectLocality = "London",
                subjectCountry = "GB",
                subjectCommonName = "www.theguardian.com",
                issuerOrganization = "GlobalSign nv-sa",
                issuerCommonName = "GlobalSign GCC R3 OV TLS CA 2024",
            ),
            certificate.toCertificateDetails(),
        )
    }

    @Test
    fun `GIVEN a certificate with only common names WHEN mapped THEN missing fields are null`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "CN=R13, O=Let's Encrypt, C=US")

        assertEquals(
            CertificateDetails(
                subjectCommonName = "example.com",
                issuerOrganization = "Let's Encrypt",
                issuerCommonName = "R13",
            ),
            certificate.toCertificateDetails(),
        )
    }

    @Test
    fun `GIVEN values with non-ASCII characters WHEN mapped THEN values are kept`() {
        val certificate = createCertificate(subject = "CN=example.com, O=Société Générale, L=Paris", issuer = "CN=CA")

        assertEquals("Société Générale", certificate.toCertificateDetails().subjectOrganization)
    }

    @Test
    fun `GIVEN values with escaped separators WHEN parsed THEN values are unescaped`() {
        val attributes = parseDistinguishedName("""CN=example.com,O=Example\, Inc.,L=Saint\+Denis""")

        assertEquals("Example, Inc.", attributes["O"])
        assertEquals("Saint+Denis", attributes["L"])
    }

    @Test
    fun `GIVEN multi-valued and repeated attributes WHEN parsed THEN the first value of each type is kept`() {
        val attributes = parseDistinguishedName("CN=first.example+O=Example,CN=second.example")

        assertEquals("first.example", attributes["CN"])
        assertEquals("Example", attributes["O"])
    }

    @Test
    fun `GIVEN blank values WHEN parsed THEN they are skipped`() {
        val attributes = parseDistinguishedName("O=,CN=example.com")

        assertNull(attributes["O"])
        assertEquals("example.com", attributes["CN"])
    }

    @Test
    fun `GIVEN all subject fields WHEN mapped to issued to THEN all fields are shown with the country name`() {
        val certificateDetails =
            CertificateDetails(
                subjectOrganization = "The Guardian",
                subjectLocality = "London",
                subjectCountry = "GB",
                subjectCommonName = "www.theguardian.com",
            )

        assertEquals(
            CertificateDetails.IssuedTo(organization = "The Guardian", locality = "London", country = "United Kingdom"),
            certificateDetails.toIssuedTo(Locale.ENGLISH),
        )
    }

    @Test
    fun `GIVEN some subject fields are missing WHEN mapped to issued to THEN only the present fields are shown`() {
        val certificateDetails = CertificateDetails(subjectOrganization = "Mozilla Corporation", subjectLocality = " ")

        assertEquals(
            CertificateDetails.IssuedTo(organization = "Mozilla Corporation", locality = null, country = null),
            certificateDetails.toIssuedTo(Locale.ENGLISH),
        )
    }

    @Test
    fun `GIVEN all subject fields are missing WHEN mapped to issued to THEN nothing is shown`() {
        assertNull(CertificateDetails(subjectCommonName = "example.com").toIssuedTo(Locale.ENGLISH))
    }

    @Test
    fun `GIVEN no certificate WHEN mapped to issued to THEN nothing is shown`() {
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

    private fun createCertificate(subject: String, issuer: String): X509Certificate =
        Mockito.mock(X509Certificate::class.java).also {
            whenever(it.subjectX500Principal).thenReturn(X500Principal(subject))
            whenever(it.issuerX500Principal).thenReturn(X500Principal(issuer))
        }
}
