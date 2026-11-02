/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class ReaderModePanelTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val listenDescription = testContext.getString(R.string.reader_mode_panel_listen_content_description)
    private val customizeDescription = testContext.getString(R.string.browser_menu_customize_reader_view_2)

    @Test
    fun `GIVEN listening is enabled THEN both buttons are shown`() {
        setPanel(isListeningEnabled = true)

        composeTestRule.onNodeWithContentDescription(listenDescription).assertExists()
        composeTestRule.onNodeWithContentDescription(customizeDescription).assertExists()
    }

    @Test
    fun `GIVEN listening is disabled THEN only the customize button is shown`() {
        setPanel(isListeningEnabled = false)

        composeTestRule.onNodeWithContentDescription(listenDescription).assertDoesNotExist()
        composeTestRule.onNodeWithContentDescription(customizeDescription).assertExists()
    }

    private fun setPanel(isListeningEnabled: Boolean) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                ReaderModePanel(
                    isListeningEnabled = isListeningEnabled,
                    onListenClicked = {},
                    onCustomizeReaderViewClicked = {},
                )
            }
        }
    }
}
