/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.view.ContextThemeWrapper
import androidx.core.view.ViewCompat
import java.security.cert.X509Certificate
import javax.security.auth.x500.X500Principal
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.any
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.mock
import org.mozilla.focus.R
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(qualifiers = "en")
class CertificateDetailsPanelTest {

    private val context = ContextThemeWrapper(testContext, R.style.AppTheme)

    private val certificate =
        createCertificate(
            subject = "CN=www.mozilla.org, O=Mozilla Corporation, L=San Francisco, C=US",
            issuer = "CN=R13",
        )

    @Test
    fun `GIVEN a website without a QWAC THEN who the certificate was issued to and verified by is shown without the trust mark`() {
        val panel = createPanel(qwac = null)

        assertEquals("www.mozilla.org", panel.text(R.id.site_host))
        assertTrue(ViewCompat.isAccessibilityHeading(panel.findViewById(R.id.site_host)!!))
        assertTrue(ViewCompat.isAccessibilityHeading(panel.findViewById(R.id.secure_connection_title)!!))
        assertEquals("Mozilla Corporation", panel.text(R.id.issued_to_organization))
        assertEquals("San Francisco", panel.text(R.id.issued_to_locality))
        assertEquals("United States", panel.text(R.id.issued_to_country))
        assertEquals("R13", panel.text(R.id.verified_by_issuer))
        assertFalse(panel.isShown(R.id.qwac_statement))
        assertFalse(panel.isShown(R.id.eu_trust_mark))
    }

    @Test
    fun `GIVEN a website with a QWAC THEN the QWAC details, the QWAC statement and the trust mark are shown`() {
        val qwac =
            createCertificate(
                subject = "CN=eidas.ec.europa.eu, O=The European Commission, L=Brussels, C=BE",
                issuer = "CN=Qualified CA, O=Qualified Issuer",
            )

        val panel = createPanel(qwac = qwac)

        assertEquals("The European Commission", panel.text(R.id.issued_to_organization))
        assertEquals("Qualified Issuer", panel.text(R.id.verified_by_issuer))
        assertTrue(panel.isShown(R.id.qwac_statement))
        assertTrue(panel.isShown(R.id.eu_trust_mark))
    }

    @Test
    fun `GIVEN a certificate without subject or issuer names THEN the issued to and verified by sections are hidden`() {
        val panel =
            createPanel(certificate = createCertificate(subject = "DC=example", issuer = "DC=example"), qwac = null)

        assertFalse(panel.isShown(R.id.issued_to))
        assertFalse(panel.isShown(R.id.verified_by))
    }

    @Test
    fun `GIVEN a website without a QWAC THEN the drag handle is shown with the default space under it`() {
        val panel = createPanel(qwac = null)

        assertTrue(panel.isShown(R.id.drag_handle))
        assertEquals(panel.dimension(R.dimen.bottom_sheet_drag_handle_bottom_margin), panel.dragHandleBottomMargin())
    }

    @Test
    fun `GIVEN a website with a QWAC THEN the drag handle has more space under it`() {
        val panel = createPanel(qwac = certificate)

        assertEquals(
            panel.dimension(R.dimen.bottom_sheet_drag_handle_bottom_margin_qwac),
            panel.dragHandleBottomMargin(),
        )
    }

    @Test
    fun `THEN the trust mark is at least 64 pixels wide`() {
        val panel = createPanel(qwac = certificate)

        assertTrue(panel.findViewById<ImageView>(R.id.eu_trust_mark)!!.layoutParams.width >= 64)
    }

    @Test
    fun `WHEN the back button is clicked THEN the tracking protection panel is shown again`() {
        var wentBack = 0
        val panel = createPanel(qwac = null, goBack = { wentBack++ })

        panel.findViewById<View>(R.id.details_back)!!.performClick()

        assertEquals(1, wentBack)
        assertFalse(panel.isShowing)
    }

    @Test
    fun `WHEN system back is pressed THEN the tracking protection panel is shown again`() {
        var wentBack = 0
        val panel = createPanel(qwac = null, goBack = { wentBack++ })

        panel.onBackPressedDispatcher.onBackPressed()

        assertEquals(1, wentBack)
        assertFalse(panel.isShowing)
    }

    private fun createPanel(
        certificate: X509Certificate? = this.certificate,
        qwac: X509Certificate?,
        goBack: () -> Unit = {},
    ): CertificateDetailsPanel {
        val engineSession = mock(EngineSession::class.java)
        doAnswer {
                @Suppress("UNCHECKED_CAST") (it.arguments[0] as (X509Certificate?) -> Unit).invoke(qwac)
            }
            .`when`(engineSession)
            .qwacStatus(any())

        return CertificateDetailsPanel(
                context = context,
                qwacStatusRequest = QwacStatusRequest(engineSession),
                tabUrl = "https://www.mozilla.org/en-US/",
                certificate = certificate,
                goBack = goBack,
            )
            .also { it.show() }
    }

    private fun CertificateDetailsPanel.text(id: Int) = findViewById<TextView>(id)!!.text.toString()

    private fun CertificateDetailsPanel.isShown(id: Int) = findViewById<View>(id)!!.visibility == View.VISIBLE

    private fun CertificateDetailsPanel.dimension(id: Int) = context.resources.getDimensionPixelSize(id)

    private fun CertificateDetailsPanel.dragHandleBottomMargin() =
        (findViewById<View>(R.id.drag_handle_bar)!!.layoutParams as ViewGroup.MarginLayoutParams).bottomMargin

    private fun createCertificate(subject: String, issuer: String): X509Certificate =
        mock(X509Certificate::class.java).also {
            whenever(it.subjectX500Principal).thenReturn(X500Principal(subject))
            whenever(it.issuerX500Principal).thenReturn(X500Principal(issuer))
        }
}
