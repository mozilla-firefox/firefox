/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabitems

import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeLeft
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.draggable.ItemInteractionState
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.telemetry.OnTabClose
import org.mozilla.fenix.tabstray.telemetry.TabCloseType

/**
 * Note - this test runs in the androidTest directory due to difficulties handling the Bitmaps coming from
 * [org.mozilla.fenix.compose.ThumbnailImage] in Robolectric
 */
@RunWith(AndroidJUnit4::class)
class TabGridTabItemTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun verifyDraggedItemScale() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = true))
        }
        composeTestRule.waitUntil("Dragged item is scaled at 75%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[ScaleKey] == 0.75f
        }
    }

    @Test
    fun verifyUndraggedItemScale() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = false))
        }
        composeTestRule.waitUntil("Undragged item is scaled at 100%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[ScaleKey] == 1f
        }
    }

    @Test
    fun verifyDraggedItemAlpha() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = true))
        }
        composeTestRule.waitUntil("Dragged item opacity is 70%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[AlphaKey] == 0.7f
        }
    }

    @Test
    fun verifyUndraggedItemAlpha() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = false))
        }
        composeTestRule.waitUntil("Undragged item opacity is 100%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[AlphaKey] == 1f
        }
    }

    @Test
    fun verifyHeldUndraggedItemAlpha() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = false, isHeld = true))
        }
        composeTestRule.waitUntil("Held item opacity is 100%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[AlphaKey] == 1f
        }
    }

    @Test
    fun verifyHeldUndraggedItemScale() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = false, isHeld = true))
        }
        composeTestRule.waitUntil("Held item scale is 100%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[ScaleKey] == 1f
        }
    }

    @Test
    fun verifyHeldItemAlpha() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = true, isHeld = true))
        }
        composeTestRule.waitUntil("Held item opacity is 70%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[AlphaKey] == 0.7f
        }
    }

    @Test
    fun verifyHeldItemScale() {
        composeTestRule.setContent {
            ComposableUnderTest(interactionState = ItemInteractionState(isDragged = true, isHeld = true))
        }
        composeTestRule.waitUntil("Held item scale is 75%") {
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).fetchSemanticsNode().config[ScaleKey] == 0.75f
        }
    }

    @Test
    fun verifyMediaIndicatorVisible() {
        composeTestRule.setContent {
            ComposableUnderTest(isMediaActive = true)
        }
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_ITEM_MEDIA_INDICATOR,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
    }

    @Test
    fun verifyMediaIndicatorNotVisible() {
        composeTestRule.setContent {
            ComposableUnderTest(isMediaActive = false)
        }
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_MEDIA_INDICATOR).assertDoesNotExist()
    }

    @Test
    fun verifyTappingCloseButtonPassesTapCloseType() {
        var closeType: TabCloseType? = null
        composeTestRule.setContent {
            ComposableUnderTest(onTabClose = { _, type -> closeType = type })
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_CLOSE).performClick()

        composeTestRule.runOnIdle { assertEquals(TabCloseType.TAP, closeType) }
    }

    @Test
    fun verifySwipingItemPassesSwipeCloseType() {
        var closeType: TabCloseType? = null
        composeTestRule.setContent {
            ComposableUnderTest(onTabClose = { _, type -> closeType = type })
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_ITEM_ROOT).performTouchInput { swipeLeft() }

        composeTestRule.runOnIdle { assertEquals(TabCloseType.SWIPE, closeType) }
    }

    @Composable
    private fun ComposableUnderTest(
        interactionState: ItemInteractionState = ItemInteractionState(),
        isMediaActive: Boolean = false,
        onTabClose: OnTabClose = { _, _ -> },
    ) {
        TabGridTabItem(
            tab = createTab(url = "mozilla.org", isMediaActive = isMediaActive),
            swipeToDismissBoxState = rememberSwipeToDismissBoxState(),
            swipingEnabled = true,
            interactionState = interactionState,
            onTabClose = onTabClose,
            onClick = { _ -> },
        )
    }
}
