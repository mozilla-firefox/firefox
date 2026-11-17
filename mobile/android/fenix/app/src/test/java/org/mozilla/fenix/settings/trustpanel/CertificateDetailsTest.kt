/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import io.mockk.every
import io.mockk.mockk
import java.security.cert.X509Certificate
import javax.security.auth.x500.X500Principal
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.settings.trustpanel.store.CertificateDetails
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.settings.trustpanel.store.parseDistinguishedName
import org.mozilla.fenix.settings.trustpanel.store.toCertificateDetails

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
    fun `GIVEN a website without a certificate THEN certificate details are null`() {
        assertNull(WebsiteInfoState(isSecured = false, certificate = null).certificateDetails)
    }

    @Test
    fun `GIVEN a website with a certificate THEN certificate details are mapped from it`() {
        val certificate = createCertificate(subject = "CN=example.com, O=Example", issuer = "CN=Issuer")

        assertEquals("Example", WebsiteInfoState(certificate = certificate).certificateDetails?.subjectOrganization)
    }

    @Test
    fun `GIVEN a website with a QWAC that differs from its certificate THEN certificate details are mapped from the QWAC`() {
        val certificate = createCertificate(subject = "CN=example.com, O=Example", issuer = "CN=Issuer")
        val qwac = createCertificate(subject = "CN=example.com, O=Example Qualified", issuer = "CN=Qualified Issuer")

        assertEquals(
            "Example Qualified",
            WebsiteInfoState(certificate = certificate, qwac = qwac).certificateDetails?.subjectOrganization,
        )
    }

    @Test
    fun `GIVEN a secure website with a certificate THEN the certificate details can be opened`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "CN=Issuer")

        assertTrue(WebsiteInfoState(isSecured = true, certificate = certificate).canOpenCertificateDetails)
    }

    @Test
    fun `GIVEN a secure website without a certificate THEN the certificate details cannot be opened`() {
        assertFalse(WebsiteInfoState(isSecured = true, certificate = null).canOpenCertificateDetails)
    }

    @Test
    fun `GIVEN an insecure website with a certificate THEN the certificate details cannot be opened`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "CN=Issuer")

        assertFalse(WebsiteInfoState(isSecured = false, certificate = certificate).canOpenCertificateDetails)
    }

    @Test
    fun `GIVEN values with escaped separators WHEN parsed THEN values are unescaped`() {
        val attributes = parseDistinguishedName("""CN=example.com,O=Example\, Inc.,L=Saint\+Denis""")

        assertEquals("Example, Inc.", attributes["O"])
        assertEquals("Saint+Denis", attributes["L"])
    }

    @Test
    fun `GIVEN values with non-ASCII characters WHEN mapped THEN values are kept`() {
        val certificate = createCertificate(subject = "CN=example.com, O=Société Générale, L=Paris", issuer = "CN=CA")

        assertEquals("Société Générale", certificate.toCertificateDetails().subjectOrganization)
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

    private fun createCertificate(subject: String, issuer: String): X509Certificate =
        mockk<X509Certificate>().also {
            every { it.subjectX500Principal } returns X500Principal(subject)
            every { it.issuerX500Principal } returns X500Principal(issuer)
        }
}
