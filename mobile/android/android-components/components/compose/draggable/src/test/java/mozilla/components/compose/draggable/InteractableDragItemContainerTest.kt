/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class InteractableDragItemContainerTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val keys = listOf("a", "b", "c", "d")
    private val manyKeys = (0 until 30).map { "item$it" }
    private val decoratedTag = "decorated"

    @Test
    fun `GIVEN entering item key WHEN grid composes THEN only that item receives the decoration`() {
        composeTestRule.setContent {
            TestGrid(enteringItemKey = "b")
        }
        composeTestRule.onAllNodesWithTag(decoratedTag).assertCountEquals(1)
    }

    @Test
    fun `GIVEN no entering item key WHEN grid composes THEN no item receives a decoration`() {
        composeTestRule.setContent {
            TestGrid(enteringItemKey = null)
        }
        composeTestRule.onAllNodesWithTag(decoratedTag).assertCountEquals(0)
    }

    @Test
    fun `GIVEN an entering item key that does not match any item WHEN grid composes THEN no item receives a decoration`() {
        composeTestRule.setContent {
            TestGrid(enteringItemKey = "NotARealKey")
        }
        composeTestRule.onAllNodesWithTag(decoratedTag).assertCountEquals(0)
    }

    @Test
    fun `GIVEN a dragged list item WHEN it is moved THEN it is placed where it was moved right away while the other items animate to their place`() {
        var listKeys by mutableStateOf(listOf("a", "b"))
        composeTestRule.setContent {
            TestList(keys = listKeys, draggedKey = "a")
        }
        composeTestRule.mainClock.autoAdvance = false

        listKeys = listOf("b", "a")
        composeTestRule.waitForIdle()
        composeTestRule.mainClock.advanceTimeByFrame()

        assertEquals(24.dp, composeTestRule.onNodeWithTag("a").getUnclippedBoundsInRoot().top)
        assertTrue(composeTestRule.onNodeWithTag("b").getUnclippedBoundsInRoot().top > 0.dp)
    }

    @Test
    fun `GIVEN a list item let go of at the top WHEN the list moves it THEN it is drawn at the top right away while the other items animate to their place`() {
        var listKeys by mutableStateOf(listOf("a", "b"))
        composeTestRule.setContent {
            TestList(keys = listKeys, draggedKey = null, droppedKey = "a")
        }
        composeTestRule.mainClock.autoAdvance = false

        listKeys = listOf("b", "a")
        composeTestRule.waitForIdle()
        composeTestRule.mainClock.advanceTimeByFrame()

        assertEquals(0.dp, composeTestRule.onNodeWithTag("a").getUnclippedBoundsInRoot().top)
        assertTrue(composeTestRule.onNodeWithTag("b").getUnclippedBoundsInRoot().top > 0.dp)
    }

    @Composable
    private fun TestList(keys: List<String>, draggedKey: String?, droppedKey: String? = null) {
        val listState = rememberLazyListState()
        val interactionState = remember { DraggedItemListState(draggedKey, droppedKey, listState) }
        LazyColumn(state = listState) {
            itemsIndexed(items = keys, key = { _, key -> key }) { index, key ->
                InteractableDragItemContainer(state = interactionState, key = key, position = index) {
                    Box(Modifier.size(24.dp).testTag(key))
                }
            }
        }
    }

    /**
     * [ListInteractionState] of the list with [listState] in which the item with [draggedKey] is dragged and kept where
     * it is laid out, or the one with [droppedKey] was just let go of at the top of the list.
     */
    private class DraggedItemListState(
        draggedKey: String?,
        droppedKey: String?,
        private val listState: LazyListState,
    ) : ListInteractionState {
        override val draggedItem: InteractionState.List =
            draggedKey?.let { InteractionState.List.Active(index = 0, key = it, size = 0, initialOffset = 0f) }
                ?: InteractionState.List.None
        override val hoveredItem = InteractionState.List.None
        override val highlightedRect: Rect? = null
        override val interactionMode = InteractionMode.List.None
        override val previousKeyOfDraggedItem: Any? = droppedKey
        override val orientation = Orientation.Vertical

        override fun onTouchSlopPassed(offset: Float, shouldLongPress: Boolean) = Unit

        override fun onDrag(offset: Float, preserveSelectMode: Boolean) = Unit

        override fun onDragEnd() = Unit

        override fun onDragCancelled() = Unit

        override fun computeItemOffset(index: Int) = 0f

        override fun onDraggedItemPositioned(itemCoordinates: LayoutCoordinates) = Unit

        override fun updateListLayoutCoordinates(coordinates: LayoutCoordinates) = Unit

        override fun computeDroppedItemOffset() =
            listState.layoutInfo.visibleItemsInfo
                .firstOrNull { it.key == previousKeyOfDraggedItem }
                ?.let { -it.offset.toFloat() } ?: 0f

        override fun reset() = Unit
    }

    @Test
    fun `GIVEN a dragged grid item WHEN it is scrolled out of view THEN it stays composed`() {
        val disposed = mutableListOf<String>()
        lateinit var gridState: LazyGridState
        lateinit var interactionState: GridInteractionState
        composeTestRule.setContent {
            gridState = rememberLazyGridState()
            interactionState =
                createGridInteractionState(
                    gridState = gridState,
                    itemInteractionHandler = NoOpItemInteractionHandler,
                    ignoredItems = emptySet(),
                    liveReorderEnabled = false,
                )
            LazyVerticalGrid(columns = GridCells.Fixed(2), modifier = Modifier.size(48.dp), state = gridState) {
                itemsIndexed(items = manyKeys, key = { _, key -> key }) { index, key ->
                    InteractableDragItemContainer(
                        state = interactionState,
                        key = key,
                        position = index,
                        swipingActive = false,
                    ) {
                        Box(Modifier.size(24.dp))
                        DisposableEffect(Unit) { onDispose { disposed += key } }
                    }
                }
            }
        }
        composeTestRule.runOnIdle { interactionState.onTouchSlopPassed(Offset(1f, 1f), shouldLongPress = false) }

        composeTestRule.runOnIdle { runBlocking { gridState.scrollToItem(manyKeys.lastIndex) } }
        composeTestRule.waitForIdle()

        assertTrue(manyKeys[1] in disposed)
        assertFalse(manyKeys[0] in disposed)
    }

    @Test
    fun `GIVEN a dragged list item WHEN it is scrolled out of view THEN it stays composed`() {
        val disposed = mutableListOf<String>()
        lateinit var listState: LazyListState
        lateinit var interactionState: ListInteractionState
        composeTestRule.setContent {
            listState = rememberLazyListState()
            interactionState =
                createListInteractionState(
                    listState = listState,
                    ignoredItems = emptySet(),
                    liveReorderEnabled = false,
                    itemInteractionHandler = NoOpItemInteractionHandler,
                )
            LazyColumn(modifier = Modifier.size(48.dp), state = listState) {
                itemsIndexed(items = manyKeys, key = { _, key -> key }) { index, key ->
                    InteractableDragItemContainer(state = interactionState, key = key, position = index) {
                        Box(Modifier.size(24.dp))
                        DisposableEffect(Unit) { onDispose { disposed += key } }
                    }
                }
            }
        }
        composeTestRule.runOnIdle { interactionState.onTouchSlopPassed(1f, shouldLongPress = false) }

        composeTestRule.runOnIdle { runBlocking { listState.scrollToItem(manyKeys.lastIndex) } }
        composeTestRule.waitForIdle()

        assertTrue(manyKeys[1] in disposed)
        assertFalse(manyKeys[0] in disposed)
    }

    @Composable
    private fun TestGrid(enteringItemKey: String?) {
        val gridState = rememberLazyGridState()
        val interactionState =
            createGridInteractionState(
                gridState = gridState,
                itemInteractionHandler = NoOpItemInteractionHandler,
                ignoredItems = emptySet(),
                liveReorderEnabled = false,
            )
        LazyVerticalGrid(columns = GridCells.Fixed(2), state = gridState) {
            itemsIndexed(items = keys, key = { _, key -> key }) { index, key ->
                InteractableDragItemContainer(
                    state = interactionState,
                    key = key,
                    position = index,
                    swipingActive = false,
                    enteringItemKey = enteringItemKey,
                    enteringItemDecoration = {
                        Modifier.testTag(decoratedTag)
                    },
                ) {
                    Box(Modifier.size(24.dp))
                }
            }
        }
    }
}
