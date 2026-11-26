/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
@Config(qualifiers = "w800dp-h320dp")
class EditMenuItemsAutoScrollTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val hiddenItemsTitle = testContext.getString(R.string.mozac_menu_customize_hidden_items)
    private val dragToHide = testContext.getString(R.string.mozac_menu_customize_drag_items_to_hide)
    private val hapticFeedback = RecordingHapticFeedback()

    @Test
    fun `GIVEN no hidden items AND a screen too short to show the area to hide items WHEN a shown item is held at the bottom of the screen THEN the list scrolls until the whole area is shown`() {
        val store = setContent(CustomizableMenuItemsState(items = List(6) { item("Shown $it") }))
        assertTrue(composeTestRule.scrollRange().maxValue() > composeTestRule.scrollRange().value())
        pickUpItemAndMoveToBottom("Shown 1")

        composeTestRule.holdStill(moves = 60)
        val stoppedAt = composeTestRule.scrollRange().value()
        composeTestRule.holdStill(moves = 20)

        assertTrue(stoppedAt > 0f)
        assertEquals(stoppedAt, composeTestRule.scrollRange().value())
        assertEquals(composeTestRule.scrollRange().maxValue(), composeTestRule.scrollRange().value())
        assertTrue(unclippedBottomOf(dragToHide) <= rootBottom())
        assertTrue(unclippedBottomOf(hiddenItemsTitle) <= rootBottom())
        assertTrue(unclippedTopOf(dragToHide) >= 0f)
        assertTrue(unclippedTopOf(hiddenItemsTitle) >= 0f)

        dropIn(dragToHide)
        assertEquals(listOf("Shown 1"), store.state.hiddenItems.map { it.id })
    }

    @Test
    fun `GIVEN hidden items AND a screen too short to show the last one WHEN a shown item is held at the bottom of the screen THEN the list scrolls until the last hidden item is shown`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = List(5) { item("Shown $it") },
                    hiddenItems = List(2) { item("Hidden $it") },
                )
            )
        assertTrue(composeTestRule.scrollRange().maxValue() > composeTestRule.scrollRange().value())
        pickUpItemAndMoveToBottom("Shown 1")

        composeTestRule.holdStill(moves = 60)
        val stoppedAt = composeTestRule.scrollRange().value()
        composeTestRule.holdStill(moves = 20)

        assertTrue(stoppedAt > 0f)
        assertEquals(stoppedAt, composeTestRule.scrollRange().value())
        assertEquals(composeTestRule.scrollRange().maxValue(), composeTestRule.scrollRange().value())
        assertTrue(unclippedBottomOf("Hidden 1") <= rootBottom())
        assertTrue(unclippedTopOf("Hidden 1") >= 0f)

        val hiddenBottom = boundsOf("Hidden 1").bottom
        composeTestRule.onRoot().performTouchInput { moveTo(Offset(center.x, hiddenBottom - 1f)) }
        composeTestRule.mainClock.advanceTimeByFrame()
        assertTrue(unclippedBottomOf("Shown 1") > rootBottom())
        val feltBefore = hapticFeedback.performed.size
        composeTestRule.holdStill(moves = 40)

        assertEquals(feltBefore, hapticFeedback.performed.size)
        assertEquals(stoppedAt, composeTestRule.scrollRange().value())
        composeTestRule.mainClock.autoAdvance = true
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()
        assertEquals(listOf("Hidden 0", "Hidden 1", "Shown 1"), store.state.hiddenItems.map { it.id })
    }

    private fun setContent(state: CustomizableMenuItemsState): CustomizableMenuItemsStore {
        val store = CustomizableMenuItemsStore(initialState = state)
        composeTestRule.setContent {
            AcornTheme {
                CompositionLocalProvider(LocalHapticFeedback provides hapticFeedback) {
                    EditMenuItemsContent(store = store)
                }
            }
        }
        return store
    }

    /** Hold the row with [title] and drag it in steps to near the bottom of the screen, still held. */
    private fun pickUpItemAndMoveToBottom(title: String) =
        composeTestRule.pickUpAndMoveToBottom(from = boundsOf(title).center, distanceFromBottom = 10f)

    /** Move the held item in steps to the middle of [text] and let go of it. */
    private fun dropIn(text: String) {
        composeTestRule.mainClock.autoAdvance = true
        var from = Offset.Zero
        composeTestRule.onRoot().performTouchInput { from = checkNotNull(currentPosition()) }
        val step = (boundsOf(text).center - from) / 10f
        repeat(10) {
            composeTestRule.onRoot().performTouchInput { moveBy(step) }
            composeTestRule.waitForIdle()
        }
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()
    }

    private fun boundsOf(text: String) = nodeOf(text).fetchSemanticsNode().boundsInRoot

    private fun unclippedTopOf(text: String) =
        with(composeTestRule.density) { nodeOf(text).getUnclippedBoundsInRoot().top.toPx() }

    private fun unclippedBottomOf(text: String) =
        with(composeTestRule.density) { nodeOf(text).getUnclippedBoundsInRoot().bottom.toPx() }

    private fun nodeOf(text: String) =
        composeTestRule.onNode(
            hasText(text) or
                SemanticsMatcher("Row of $text") { node ->
                    val description =
                        node.config.getOrElse(SemanticsProperties.ContentDescription) { emptyList() }.singleOrNull()
                    description == text || description?.startsWith("$text. Position ") == true
                }
        )

    private fun rootBottom() = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot.bottom

    /** [HapticFeedback] that records every feedback type it is asked to perform, in order, in [performed]. */
    private class RecordingHapticFeedback : HapticFeedback {
        val performed = mutableListOf<HapticFeedbackType>()

        override fun performHapticFeedback(hapticFeedbackType: HapticFeedbackType) {
            performed += hapticFeedbackType
        }
    }

    private fun item(title: String) =
        CustomizableMenuItem(id = title, item = StandardMenuItem(title = MenuItemTitle(Text.String(title))))
}
