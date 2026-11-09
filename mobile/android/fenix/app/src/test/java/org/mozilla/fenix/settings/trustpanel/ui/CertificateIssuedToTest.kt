/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.content.Context
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
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
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.trustpanel.store.CertificateDetails
import org.mozilla.fenix.theme.FirefoxTheme
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
@Config(qualifiers = "en")
class CertificateIssuedToTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val label
        get() =
            ApplicationProvider.getApplicationContext<Context>()
                .getString(R.string.certificate_details_panel_issued_to_label)

    @Test
    fun `GIVEN all subject fields THEN the label, organization, locality and country are shown`() {
        setCertificateIssuedTo(
            CertificateDetails(subjectOrganization = "The Guardian", subjectLocality = "London", subjectCountry = "GB")
        )

        composeTestRule.onNodeWithText(label, useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("The Guardian", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("London", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("United Kingdom", useUnmergedTree = true).assertIsDisplayed()
    }

    @Test
    fun `GIVEN all subject fields THEN the label and values are read as one item`() {
        setCertificateIssuedTo(
            CertificateDetails(subjectOrganization = "The Guardian", subjectLocality = "London", subjectCountry = "GB")
        )

        val announcedTexts =
            composeTestRule.onNode(hasText(label)).fetchSemanticsNode().config[SemanticsProperties.Text].map { it.text }

        assertEquals(listOf(label, "The Guardian", "London", "United Kingdom"), announcedTexts)
    }

    @Test
    fun `GIVEN some subject fields are missing THEN only the present fields are shown`() {
        setCertificateIssuedTo(CertificateDetails(subjectOrganization = "Mozilla Corporation"))

        composeTestRule.onNodeWithText(label, useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("Mozilla Corporation", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule
            .onAllNodes(SemanticsMatcher.keyIsDefined(SemanticsProperties.Text), useUnmergedTree = true)
            .assertCountEquals(2)
    }

    @Test
    fun `GIVEN all subject fields are missing THEN nothing is shown`() {
        setCertificateIssuedTo(CertificateDetails(subjectCommonName = "example.com"))

        composeTestRule.onRoot(useUnmergedTree = true).onChildren().assertCountEquals(0)
    }

    private fun setCertificateIssuedTo(certificateDetails: CertificateDetails) {
        composeTestRule.setContent {
            FirefoxTheme(Theme.Light) { CertificateIssuedTo(certificateDetails = certificateDetails) }
        }
    }
}
