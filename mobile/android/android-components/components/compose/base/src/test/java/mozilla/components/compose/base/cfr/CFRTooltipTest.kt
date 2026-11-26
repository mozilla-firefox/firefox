/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.cfr

import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Text
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@OptIn(ExperimentalMaterial3Api::class)
@RunWith(AndroidJUnit4::class)
class CFRTooltipTest {

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN user input is enabled WHEN the anchor is long-pressed THEN the CFR is shown`() {
        setCFRTooltipContent(enableUserInput = true)

        composeTestRule.onNodeWithText(ANCHOR_TEXT).performTouchInput { longClick() }

        composeTestRule.onNodeWithText(CFR_TEXT).assertIsDisplayed()
    }

    @Test
    fun `GIVEN user input is disabled WHEN the anchor is long-pressed THEN the CFR is not shown`() {
        setCFRTooltipContent(enableUserInput = false)

        composeTestRule.onNodeWithText(ANCHOR_TEXT).performTouchInput { longClick() }

        composeTestRule.onNodeWithText(CFR_TEXT).assertDoesNotExist()
    }

    private fun setCFRTooltipContent(enableUserInput: Boolean) {
        composeTestRule.setContent {
            CFRTooltip(
                cfr = { Text(text = CFR_TEXT) },
                state = rememberCFRState(),
                enableUserInput = enableUserInput,
            ) {
                Text(text = ANCHOR_TEXT)
            }
        }
    }
}

private const val ANCHOR_TEXT = "Anchor"
private const val CFR_TEXT = "Did you know?"
