/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.content.Context
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onChildren
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.utils.CertificateDetails
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class CertificateVerifiedByTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val label
        get() =
            ApplicationProvider.getApplicationContext<Context>()
                .getString(R.string.certificate_details_panel_verified_by_label)

    @Test
    fun `GIVEN an issuer organization THEN the label and organization are shown`() {
        setCertificateVerifiedBy(
            CertificateDetails(
                issuerOrganization = "GlobalSign nv-sa",
                issuerCommonName = "GlobalSign GCC R3 OV TLS CA",
            )
        )

        composeTestRule.onNodeWithText(label, useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("GlobalSign nv-sa", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("GlobalSign GCC R3 OV TLS CA", useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN an issuer organization THEN the label and organization are read as one item`() {
        setCertificateVerifiedBy(
            CertificateDetails(
                issuerOrganization = "GlobalSign nv-sa",
                issuerCommonName = "GlobalSign GCC R3 OV TLS CA",
            )
        )

        assertEquals(listOf(label, "GlobalSign nv-sa"), announcedTexts())
    }

    @Test
    fun `GIVEN only an issuer common name THEN the label and common name are read as one item`() {
        setCertificateVerifiedBy(CertificateDetails(issuerCommonName = "R13"))

        assertEquals(listOf(label, "R13"), announcedTexts())
    }

    @Test
    fun `GIVEN neither an issuer organization nor a common name THEN nothing is shown`() {
        setCertificateVerifiedBy(CertificateDetails(subjectOrganization = "Mozilla Corporation"))

        composeTestRule.onRoot(useUnmergedTree = true).onChildren().assertCountEquals(0)
    }

    private fun announcedTexts(): List<String> =
        composeTestRule.onNode(hasText(label)).fetchSemanticsNode().config[SemanticsProperties.Text].map { it.text }

    private fun setCertificateVerifiedBy(certificateDetails: CertificateDetails) {
        composeTestRule.setContent {
            FirefoxTheme(Theme.Light) { CertificateVerifiedBy(certificateDetails = certificateDetails) }
        }
    }
}
