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
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState

class CertificateDetailsTest {

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
    fun `GIVEN a secure website whose certificate has only an issuer THEN the certificate details can be opened`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "CN=R13")

        assertTrue(WebsiteInfoState(isSecured = true, certificate = certificate).canOpenCertificateDetails)
    }

    @Test
    fun `GIVEN a secure website whose certificate has nothing to show THEN the certificate details cannot be opened`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "DC=example")

        assertFalse(WebsiteInfoState(isSecured = true, certificate = certificate).canOpenCertificateDetails)
    }

    @Test
    fun `GIVEN a secure website with a QWAC THEN it is shown as qualified`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "CN=Issuer")

        assertTrue(WebsiteInfoState(isSecured = true, certificate = certificate, qwac = certificate).isQwac)
    }

    @Test
    fun `GIVEN an insecure website with a QWAC THEN it is not shown as qualified`() {
        val certificate = createCertificate(subject = "CN=example.com", issuer = "CN=Issuer")

        assertFalse(WebsiteInfoState(isSecured = false, certificate = certificate, qwac = certificate).isQwac)
    }

    private fun createCertificate(subject: String, issuer: String): X509Certificate =
        mockk<X509Certificate>().also {
            every { it.subjectX500Principal } returns X500Principal(subject)
            every { it.issuerX500Principal } returns X500Principal(issuer)
        }
}
