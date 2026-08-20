/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf.ui

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.pdf.PdfToolsTestTag
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.Theme

@RunWith(AndroidJUnit4::class)
class PdfToolsTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val clicked = mutableListOf<String>()

    private fun setTestContent(isLargeWindow: Boolean) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                PdfTools(
                    isLargeWindow = isLargeWindow,
                    onSignClick = { clicked.add("sign") },
                    onDownloadClick = { clicked.add("download") },
                    onPrintClick = { clicked.add("print") },
                    onShareClick = { clicked.add("share") },
                )
            }
        }
    }

    @Test
    fun `GIVEN a phone sized window WHEN the tools are shown THEN sign is a floating action button`() {
        setTestContent(isLargeWindow = false)

        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_FAB).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.BAR).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.DOWNLOAD_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.PRINT_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SHARE_BUTTON).assertIsDisplayed()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_FAB).performClick()
        assertEquals(listOf("sign"), clicked)

        // On a phone, sign is a FAB
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_BUTTON).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a tablet sized window WHEN the tools are shown THEN sign joins the bar`() {
        setTestContent(isLargeWindow = true)

        composeTestRule.onNodeWithTag(PdfToolsTestTag.BAR).assertIsDisplayed()
        // On a tablet, sign is on the toolbar
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.DOWNLOAD_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.PRINT_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SHARE_BUTTON).assertIsDisplayed()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_BUTTON).performClick()
        assertEquals(listOf("sign"), clicked)

        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_FAB).assertDoesNotExist()
    }
}
