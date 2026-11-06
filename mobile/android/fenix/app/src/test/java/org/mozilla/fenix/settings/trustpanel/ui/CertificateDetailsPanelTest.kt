/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.content.Context
import androidx.compose.material3.Text
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.Theme
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class CertificateDetailsPanelTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val resources
        get() = ApplicationProvider.getApplicationContext<Context>().resources

    private val qwacStatement
        get() = resources.getString(R.string.connection_security_panel_qualified_certificate)

    private val trustMarkDescription
        get() = resources.getString(R.string.certificate_details_panel_eu_trust_mark_content_description)

    @Test
    fun `GIVEN no QWAC certificate data in the light theme THEN neither the QWAC statement nor the EU trust mark is shown`() {
        setCertificateDetailsPanel(isQwac = false, theme = Theme.Light)

        assertQwacElementsDoNotExist()
        composeTestRule.onNodeWithText(VERIFIED_BY).assertIsDisplayed()
    }

    @Test
    fun `GIVEN no QWAC certificate data in the dark theme THEN neither the QWAC statement nor the EU trust mark is shown`() {
        setCertificateDetailsPanel(isQwac = false, theme = Theme.Dark)

        assertQwacElementsDoNotExist()
        composeTestRule.onNodeWithText(VERIFIED_BY).assertIsDisplayed()
    }

    @Test
    fun `GIVEN QWAC certificate data THEN both the QWAC statement and the EU trust mark are shown`() {
        setCertificateDetailsPanel(isQwac = true, theme = Theme.Light)

        composeTestRule.onNodeWithText(qwacStatement).assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription(trustMarkDescription).assertIsDisplayed()
    }

    private fun assertQwacElementsDoNotExist() {
        composeTestRule.onNodeWithText(qwacStatement, useUnmergedTree = true).assertDoesNotExist()
        composeTestRule.onNodeWithContentDescription(trustMarkDescription, useUnmergedTree = true).assertDoesNotExist()
    }

    private fun setCertificateDetailsPanel(isQwac: Boolean, theme: Theme) {
        composeTestRule.setContent {
            FirefoxTheme(theme) {
                CertificateDetailsPanel(
                    url = "https://www.mozilla.org",
                    icon = null,
                    issuedTo = { Text(text = "Issued to") },
                    verifiedBy = { Text(text = VERIFIED_BY) },
                    isQwac = isQwac,
                )
            }
        }
    }

    private companion object {
        const val VERIFIED_BY = "Verified by"
    }
}
