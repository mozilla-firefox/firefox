/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performTouchInput
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
        val from = sectionText("Shown 1").fetchSemanticsNode().boundsInRoot.center
        val bottom = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot.bottom
        composeTestRule.onRoot().performTouchInput {
            down(from)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            // Send the delay with a stationary move, an empty batch would drop it.
            move()
        }
        composeTestRule.waitForIdle()
        composeTestRule.mainClock.autoAdvance = false

        val step = (Offset(from.x, bottom - 5f) - from) / 20f
        repeat(20) {
            composeTestRule.onRoot().performTouchInput { moveBy(step) }
            composeTestRule.mainClock.advanceTimeByFrame()
        }
        // Each move scrolls the list as long as it would.
        holdFor(moves = 60)
        val stoppedAt = scrollRange().value()
        holdFor(moves = 20)

        assertTrue(stoppedAt > 0f)
        assertEquals(stoppedAt, scrollRange().value())
        // Not scrolled to the end, where the reset button would be fully shown.
        val resetButtonBottom = composeTestRule.onNodeWithText(resetToDefault).getUnclippedBoundsInRoot().bottom
        assertTrue(resetButtonBottom > composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom)
    }

    private fun holdFor(moves: Int) {
        repeat(moves) {
            composeTestRule.onRoot().performTouchInput { moveBy(Offset(0f, 0.5f)) }
            composeTestRule.mainClock.advanceTimeByFrame()
        }
    }

    private fun scrollRange() =
        composeTestRule
            .onNode(SemanticsMatcher.keyIsDefined(SemanticsProperties.VerticalScrollAxisRange))
            .fetchSemanticsNode()
            .config[SemanticsProperties.VerticalScrollAxisRange]

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
