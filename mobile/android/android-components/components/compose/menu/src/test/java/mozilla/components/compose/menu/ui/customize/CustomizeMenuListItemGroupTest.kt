/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomizeMenuListItemGroupTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN a group of more items than shown WHEN it is in a row as tall as its content THEN it is as tall as the group alone and its last item is shown as in the group alone`() {
        var isInRow by mutableStateOf(false)
        composeTestRule.setContent {
            AcornTheme {
                val group =
                    @Composable {
                        CustomizeMenuListItemGroup(
                            items = listOf("Item 1", "Item 2", "Item 3", "Item 4").map(::item),
                            modifier = Modifier.testTag("group"),
                        )
                    }
                if (isInRow) {
                    Row(Modifier.testTag("row").height(IntrinsicSize.Min)) { group() }
                } else {
                    group()
                }
            }
        }
        val groupAlone = boundsOfTag("group")
        val peekedAlone = boundsOfText("Item 3")

        isInRow = true
        composeTestRule.waitForIdle()

        assertEquals(groupAlone, boundsOfTag("group"))
        assertEquals(peekedAlone, boundsOfText("Item 3"))
        assertEquals(groupAlone.height, boundsOfTag("row").height, 0f)
    }

    private fun item(title: String) =
        StandardMenuItem(
            title = MenuItemTitle(Text.String(title)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
        )

    private fun boundsOfTag(tag: String) = composeTestRule.onNodeWithTag(tag).fetchSemanticsNode().boundsInRoot

    private fun boundsOfText(text: String) =
        composeTestRule.onNodeWithText(text, useUnmergedTree = true).fetchSemanticsNode().boundsInRoot
}
