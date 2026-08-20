/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.Theme

@RunWith(AndroidJUnit4::class)
class PdfToolsContentTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val tabId = "1"

    private val clicked = mutableListOf<String>()

    private val browserStore =
        BrowserStore(
            BrowserState(
                tabs = listOf(createTab(url = "https://mozilla.org", id = tabId)),
                selectedTabId = tabId,
            )
        )

    private fun setTestContent(isLargeWindow: Boolean) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                PdfToolsContent(
                    browserStore = browserStore,
                    isLargeWindow = isLargeWindow,
                    onDownloadClick = { clicked.add("download") },
                    onPrintClick = { clicked.add("print") },
                    onShareClick = { clicked.add("share") },
                )
            }
        }
    }

    private fun enterPdfViewer() {
        browserStore.dispatch(ContentAction.EnteredPdfViewer(tabId))
        composeTestRule.waitForIdle()
    }

    private fun exitPdfViewer() {
        browserStore.dispatch(ContentAction.ExitedPdfViewer(tabId))
        composeTestRule.waitForIdle()
    }

    @Test
    fun `GIVEN a phone sized window WHEN the selected tab is not a PDF THEN nothing is shown`() {
        setTestContent(isLargeWindow = false)

        composeTestRule.onNodeWithTag(PdfToolsTestTag.BAR).assertDoesNotExist()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_FAB).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a tablet sized window WHEN the selected tab is not a PDF THEN nothing is shown`() {
        setTestContent(isLargeWindow = true)

        composeTestRule.onNodeWithTag(PdfToolsTestTag.BAR).assertDoesNotExist()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_BUTTON).assertDoesNotExist()
    }

    @Test
    fun `WHEN the selected tab starts showing a PDF THEN the tools are shown`() {
        setTestContent(isLargeWindow = false)

        enterPdfViewer()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.BAR).assertIsDisplayed()
    }

    @Test
    fun `WHEN the selected tab stops showing a PDF THEN the tools are hidden again`() {
        setTestContent(isLargeWindow = false)

        enterPdfViewer()
        exitPdfViewer()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.BAR).assertDoesNotExist()
        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_FAB).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a tablet sized window WHEN the selected tab shows a PDF THEN the tablet layout is used`() {
        setTestContent(isLargeWindow = true)

        enterPdfViewer()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.SIGN_BUTTON).assertIsDisplayed()
    }

    @Test
    fun `WHEN the download button is tapped THEN the download callback is invoked`() {
        setTestContent(isLargeWindow = false)

        enterPdfViewer()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.DOWNLOAD_BUTTON).performClick()

        assertEquals(listOf("download"), clicked)
    }

    @Test
    fun `WHEN the print button is tapped THEN the print callback is invoked`() {
        setTestContent(isLargeWindow = false)

        enterPdfViewer()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.PRINT_BUTTON).performClick()

        assertEquals(listOf("print"), clicked)
    }

    @Test
    fun `WHEN the share button is tapped THEN the share callback is invoked`() {
        setTestContent(isLargeWindow = false)

        enterPdfViewer()

        composeTestRule.onNodeWithTag(PdfToolsTestTag.SHARE_BUTTON).performClick()

        assertEquals(listOf("share"), clicked)
    }
}
