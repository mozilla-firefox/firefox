/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.draggable.InteractableDragItemContainer
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.draggable.createListInteractionState
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomizeMenuDragGesturesTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val handler = RecordingHandler()

    @Test
    fun `GIVEN no long press callback WHEN an item is held for longer than a long press and then moved vertically THEN it is dragged`() {
        setContent()

        composeTestRule.onNodeWithTag("a").performTouchInput {
            down(center)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()
        composeTestRule.onRoot().performTouchInput {
            moveBy(Offset(0f, viewConfiguration.touchSlop * 3))
            up()
        }
        composeTestRule.waitForIdle()

        assertEquals(listOf<Any>("a"), handler.dragStarted)
        assertEquals(1, handler.dragEnded)
        assertEquals(1, handler.moved)
    }

    @Test
    fun `GIVEN no long press callback WHEN an item is moved below the touch slop before and after a long press time THEN it is dragged`() {
        setContent()

        var longPressTimeout = 0L
        composeTestRule.onNodeWithTag("a").performTouchInput {
            longPressTimeout = viewConfiguration.longPressTimeoutMillis
            down(center)
            moveBy(Offset(0f, viewConfiguration.touchSlop * 0.75f))
        }
        composeTestRule.mainClock.advanceTimeBy(longPressTimeout * 2)
        composeTestRule.waitForIdle()
        composeTestRule.onRoot().performTouchInput { moveBy(Offset(0f, viewConfiguration.touchSlop * 0.75f)) }
        composeTestRule.waitForIdle()

        assertEquals(listOf<Any>("a"), handler.dragStarted)

        composeTestRule.onRoot().performTouchInput {
            moveBy(Offset(0f, viewConfiguration.touchSlop * 3))
            up()
        }
        composeTestRule.waitForIdle()

        assertEquals(listOf<Any>("a"), handler.dragStarted)
        assertEquals(1, handler.dragEnded)
        assertEquals(1, handler.moved)
    }

    private fun setContent() {
        composeTestRule.setContent {
            val listState = rememberLazyListState()
            val interactionState =
                createListInteractionState(
                    listState = listState,
                    ignoredItems = emptySet(),
                    liveReorderEnabled = false,
                    itemInteractionHandler = handler,
                    dragAndDropEnabled = false,
                )
            LazyColumn(
                modifier = Modifier.onGloballyPositioned(interactionState::updateListLayoutCoordinates),
                state = listState,
            ) {
                listOf("a", "b", "c").forEachIndexed { index, key ->
                    item(key = key) {
                        InteractableDragItemContainer(state = interactionState, key = key, position = index) {
                            Box(
                                Modifier.height(100.dp)
                                    .dragOnVerticalMove(interactionState, listState, key, onLongPress = null)
                                    .testTag(key)
                            )
                        }
                    }
                }
            }
        }
    }

    private class RecordingHandler : ItemInteractionHandler {
        val dragStarted = mutableListOf<Any>()

        var dragEnded = 0
        var moved = 0

        override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) {
            moved++
        }

        override fun onDrop(sourceKey: Any, targetKey: Any) = Unit

        override fun onDragCancel() = Unit

        override fun onDragEnd() {
            dragEnded++
        }

        override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
            dragStarted += sourceKey
        }
    }
}
