/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.width
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.width
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MenuBannerTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN content wider than the allowed fraction THEN limit its width to that fraction`() {
        composeTestRule.setContent {
            Row(Modifier.width(200.dp)) {
                Box(Modifier.maxWidthFraction(0.5f).width(300.dp).testTag("content"))
            }
        }

        assertEquals(100.dp, composeTestRule.onNodeWithTag("content").getUnclippedBoundsInRoot().width)
    }

    @Test
    fun `GIVEN content narrower than the allowed fraction THEN keep its width`() {
        composeTestRule.setContent {
            Row(Modifier.width(200.dp)) {
                Box(Modifier.maxWidthFraction(0.5f).width(60.dp).testTag("content"))
            }
        }

        assertEquals(60.dp, composeTestRule.onNodeWithTag("content").getUnclippedBoundsInRoot().width)
    }

    @Test
    fun `GIVEN a banner narrower than twice its illustration THEN keep half of it for the text`() {
        val longTitle = "A very long banner title that fills the available text width"
        composeTestRule.setContent {
            AcornTheme {
                Box(Modifier.width(60.dp)) {
                    MenuBanner(
                        title = Text.String(longTitle),
                        summary = MenuItemSummary(text = Text.String("Summary")),
                        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
                        actionButtonText = Text.String("Button"),
                        onClick = {},
                        onDismiss = {},
                    )
                }
            }
        }

        val titleWidth =
            composeTestRule.onNodeWithText(longTitle, useUnmergedTree = true).getUnclippedBoundsInRoot().width
        assertEquals(12f, titleWidth.value, 1f)
    }
}
