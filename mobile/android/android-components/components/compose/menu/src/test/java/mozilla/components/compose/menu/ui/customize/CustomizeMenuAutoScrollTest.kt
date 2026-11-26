/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
@Config(qualifiers = "w800dp-h320dp")
class CustomizeMenuAutoScrollTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val resetToDefault = testContext.getString(R.string.mozac_menu_customize_reset_to_default)

    @Test
    fun `GIVEN a screen too short to show all sections WHEN a shown section is held at the bottom of the screen THEN the list stops scrolling before its end`() {
        val shown = List(6) { row("Shown $it") }
        val hidden = List(2) { row("Hidden $it") }
        val store =
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups = shown + hidden,
                        hiddenGroupIds = hidden.map { it.id }.toSet(),
                        isCustomized = true,
                    )
            )
        composeTestRule.setContent { AcornTheme { CustomizeMenuContent(store = store) } }
        pickUpSectionAndMoveToBottom("Shown 1")
        // Each move scrolls the list as long as it would.
        composeTestRule.holdFor(moves = 60)
        val stoppedAt = composeTestRule.scrollRange().value()
        composeTestRule.holdFor(moves = 20)

        assertTrue(stoppedAt > 0f)
        assertEquals(stoppedAt, composeTestRule.scrollRange().value())
        // Not scrolled to the end, where the reset button would be fully shown.
        val resetButtonBottom = composeTestRule.onNodeWithText(resetToDefault).getUnclippedBoundsInRoot().bottom
        assertTrue(resetButtonBottom > composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom)
    }

    @Test
    fun `GIVEN a menu not customized yet AND a screen too short to show its hidden section WHEN a shown section is held at the bottom of the screen THEN the list scrolls until the hidden section is shown`() {
        val hidden = row("Hidden 0")
        val store =
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups = List(6) { row("Shown $it") } + hidden,
                        hiddenGroupIds = setOf(hidden.id),
                        isCustomized = false,
                    )
            )
        composeTestRule.setContent { AcornTheme { CustomizeMenuContent(store = store) } }
        val rootBottom = composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom
        assertTrue(composeTestRule.scrollRange().maxValue() > composeTestRule.scrollRange().value())
        pickUpSectionAndMoveToBottom("Shown 1", distanceFromBottom = 10f)

        composeTestRule.holdStill(moves = 100)
        val stoppedAt = composeTestRule.scrollRange().value()
        composeTestRule.holdStill(moves = 20)

        assertTrue(stoppedAt > 0f)
        assertEquals(stoppedAt, composeTestRule.scrollRange().value())
        assertEquals(composeTestRule.scrollRange().maxValue(), composeTestRule.scrollRange().value())
        val hiddenBounds = sectionText("Hidden 0").getUnclippedBoundsInRoot()
        assertTrue(hiddenBounds.top >= 0.dp)
        assertTrue(hiddenBounds.bottom <= rootBottom)
    }

    /** Hold the section with [title] and move it in steps to [distanceFromBottom] above the bottom of the screen. */
    private fun pickUpSectionAndMoveToBottom(title: String, distanceFromBottom: Float = 5f) {
        composeTestRule.pickUpAndMoveToBottom(
            from = sectionText(title).fetchSemanticsNode().boundsInRoot.center,
            distanceFromBottom = distanceFromBottom,
        )
    }

    private fun row(title: String) =
        MenuItemsGroup.Row(
            id = title,
            items =
                listOf(
                    StandardMenuItem(
                        title = MenuItemTitle(Text.String(title)),
                        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                    )
                ),
        )

    /** The node of the text of a section, merged into the node of the section for accessibility services. */
    private fun sectionText(text: String) = composeTestRule.onNodeWithText(text, useUnmergedTree = true)
}
