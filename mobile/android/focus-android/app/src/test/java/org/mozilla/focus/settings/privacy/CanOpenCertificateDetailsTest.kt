/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import java.security.cert.X509Certificate
import javax.security.auth.x500.X500Principal
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.support.test.whenever
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.mock

class CanOpenCertificateDetailsTest {

    @Test
    fun `GIVEN a secure connection with a certificate THEN the certificate details can be opened`() {
        val certificate = createCertificate(subject = "CN=www.mozilla.org, O=Mozilla Corporation", issuer = "CN=R13")

        assertTrue(SecurityInfo.Secure(certificate = certificate).canOpenCertificateDetails())
    }

    @Test
    fun `GIVEN a secure connection whose certificate has only an issuer THEN the certificate details can be opened`() {
        val certificate = createCertificate(subject = "DC=example", issuer = "CN=R13")

        assertTrue(SecurityInfo.Secure(certificate = certificate).canOpenCertificateDetails())
    }

    @Test
    fun `GIVEN a secure connection whose certificate has nothing to show THEN the certificate details cannot be opened`() {
        val certificate = createCertificate(subject = "DC=example", issuer = "DC=example")

        assertFalse(SecurityInfo.Secure(certificate = certificate).canOpenCertificateDetails())
    }

    @Test
    fun `GIVEN a secure connection without a certificate THEN the certificate details cannot be opened`() {
        assertFalse(SecurityInfo.Secure(certificate = null).canOpenCertificateDetails())
    }

    @Test
    fun `GIVEN a connection whose certificate has an error THEN the certificate details cannot be opened`() {
        val certificate = createCertificate(subject = "CN=www.mozilla.org, O=Mozilla Corporation", issuer = "CN=R13")

        assertFalse(SecurityInfo.Insecure(certificate = certificate).canOpenCertificateDetails())
    }

    @Test
    fun `GIVEN an HTTP page or a page with no security information THEN the certificate details cannot be opened`() {
        assertFalse(SecurityInfo.Insecure().canOpenCertificateDetails())
        assertFalse(SecurityInfo.Unknown.canOpenCertificateDetails())
    }

    private fun createCertificate(subject: String, issuer: String): X509Certificate =
        mock(X509Certificate::class.java).also {
            whenever(it.subjectX500Principal).thenReturn(X500Principal(subject))
            whenever(it.issuerX500Principal).thenReturn(X500Principal(issuer))
        }
}
