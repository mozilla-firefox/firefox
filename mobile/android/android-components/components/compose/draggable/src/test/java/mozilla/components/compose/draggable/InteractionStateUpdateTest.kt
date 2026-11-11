/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyGridItemInfo
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import mozilla.components.compose.draggable.RecordingItemInteractionHandler.Call
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class InteractionStateUpdateTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val keys = listOf("a", "b", "c", "d")

    @Test
    fun `GIVEN a grid state WHEN recomposed with a new onLongPress THEN a long press invokes the new callback`() {
        val received = mutableListOf<String>()
        var onLongPress by mutableStateOf<(LazyGridItemInfo) -> Unit>({ received += "first:${it.key}" })
        lateinit var state: GridInteractionState
        composeTestRule.setContent {
            state = testGrid(onLongPress = onLongPress, itemInteractionHandler = NoOpItemInteractionHandler)
        }

        composeTestRule.runOnIdle { onLongPress = { received += "second:${it.key}" } }
        composeTestRule.runOnIdle { state.onTouchSlopPassed(offset = Offset(1f, 1f), shouldLongPress = true) }

        assertEquals(listOf("second:a"), received)
    }

    @Test
    fun `GIVEN a grid state WHEN recomposed with a new item interaction handler THEN a drag notifies the new handler`() {
        val firstHandler = RecordingItemInteractionHandler()
        val secondHandler = RecordingItemInteractionHandler()
        var handler by mutableStateOf<ItemInteractionHandler>(firstHandler)
        lateinit var state: GridInteractionState
        composeTestRule.setContent {
            state = testGrid(onLongPress = {}, itemInteractionHandler = handler)
        }

        composeTestRule.runOnIdle { handler = secondHandler }
        composeTestRule.runOnIdle {
            state.onTouchSlopPassed(offset = Offset(1f, 1f), shouldLongPress = false)
            state.onDrag(offset = Offset(100f, 0f), preserveSelectMode = false)
        }

        assertTrue(firstHandler.calls.isEmpty())
        assertEquals(Call.DragStart(sourceKey = "a", preserveSelectMode = false), secondHandler.calls.first())
    }

    @Test
    fun `GIVEN a list state WHEN recomposed with a new onLongPress THEN a long press invokes the new callback`() {
        val received = mutableListOf<String>()
        var onLongPress by mutableStateOf<(LazyListItemInfo) -> Unit>({ received += "first:${it.key}" })
        lateinit var state: ListInteractionState
        composeTestRule.setContent {
            state = testList(onLongPress = onLongPress, itemInteractionHandler = NoOpItemInteractionHandler)
        }

        composeTestRule.runOnIdle { onLongPress = { received += "second:${it.key}" } }
        composeTestRule.runOnIdle { state.onTouchSlopPassed(offset = 1f, shouldLongPress = true) }

        assertEquals(listOf("second:a"), received)
    }

    @Test
    fun `GIVEN a list state WHEN recomposed with a new item interaction handler THEN a drag notifies the new handler`() {
        val firstHandler = RecordingItemInteractionHandler()
        val secondHandler = RecordingItemInteractionHandler()
        var handler by mutableStateOf<ItemInteractionHandler>(firstHandler)
        lateinit var state: ListInteractionState
        composeTestRule.setContent {
            state = testList(onLongPress = {}, itemInteractionHandler = handler)
        }

        composeTestRule.runOnIdle { handler = secondHandler }
        composeTestRule.runOnIdle {
            state.onTouchSlopPassed(offset = 1f, shouldLongPress = false)
            state.onDrag(offset = 100f, preserveSelectMode = false)
        }

        assertTrue(firstHandler.calls.isEmpty())
        assertEquals(Call.DragStart(sourceKey = "a", preserveSelectMode = false), secondHandler.calls.first())
    }

    @Test
    fun `GIVEN a list state WHEN recomposed with new items which cannot be dragged THEN a drag cannot start from those items`() {
        var fixedItems by mutableStateOf<Set<Any>>(emptySet())
        lateinit var state: ListInteractionState
        composeTestRule.setContent {
            state =
                testList(
                    onLongPress = {},
                    itemInteractionHandler = NoOpItemInteractionHandler,
                    fixedItems = fixedItems,
                )
        }

        composeTestRule.runOnIdle { fixedItems = setOf("a") }
        composeTestRule.runOnIdle { state.onTouchSlopPassed(offset = 1f, shouldLongPress = false) }

        assertEquals(InteractionState.List.None, state.draggedItem)
    }

    @Composable
    private fun testGrid(
        onLongPress: (LazyGridItemInfo) -> Unit,
        itemInteractionHandler: ItemInteractionHandler,
    ): GridInteractionState {
        val gridState = rememberLazyGridState()
        val interactionState =
            createGridInteractionState(
                gridState = gridState,
                itemInteractionHandler = itemInteractionHandler,
                ignoredItems = emptySet(),
                liveReorderEnabled = false,
                onLongPress = onLongPress,
            )
        LazyVerticalGrid(columns = GridCells.Fixed(2), state = gridState) {
            items(items = keys, key = { it }) {
                Box(Modifier.size(48.dp))
            }
        }
        return interactionState
    }

    @Composable
    private fun testList(
        onLongPress: (LazyListItemInfo) -> Unit,
        itemInteractionHandler: ItemInteractionHandler,
        fixedItems: Set<Any> = emptySet(),
    ): ListInteractionState {
        val listState = rememberLazyListState()
        val interactionState =
            createListInteractionState(
                listState = listState,
                ignoredItems = emptySet(),
                liveReorderEnabled = false,
                onLongPress = onLongPress,
                itemInteractionHandler = itemInteractionHandler,
                fixedItems = fixedItems,
            )
        LazyColumn(state = listState) {
            items(items = keys, key = { it }) {
                Box(Modifier.size(48.dp))
            }
        }
        return interactionState
    }
}
