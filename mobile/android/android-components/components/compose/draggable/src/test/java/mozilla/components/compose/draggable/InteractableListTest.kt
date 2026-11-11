/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.ui.geometry.Rect
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import mozilla.components.compose.draggable.RecordingItemInteractionHandler.Call
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertTrue
import org.junit.Test

class InteractableListTest {
    private val testDispatcher = StandardTestDispatcher()
    private val scope = TestScope(testDispatcher)

    private val defaultIgnoredItems = setOf(TabKeys.HEADER, TabKeys.SPAN)

    @Test
    fun `GIVEN a y value is inside the Rect THEN closestDistanceTo returns 0`() {
        val rect =
            Rect(
                left = 0f,
                top = 0f,
                right = 100f,
                bottom = 100f,
            )
        assertEquals(0f, rect.closestDistanceTo(50f))
    }

    @Test
    fun `GIVEN a y value is to the top of a rect THEN closestDistanceTo returns squared distance from top edge`() {
        val rect =
            Rect(
                left = 0f,
                top = 0f,
                right = 100f,
                bottom = 100f,
            )
        assertEquals(100f, rect.closestDistanceTo(-10f))
    }

    @Test
    fun `GIVEN a y value is to the bottom of a rect THEN closestDistanceTo returns squared distance from bottom edge`() {
        val rect =
            Rect(
                left = 0f,
                top = 0f,
                right = 100f,
                bottom = 100f,
            )
        assertEquals(100f, rect.closestDistanceTo(110f))
    }

    @Test
    fun `Given a y value is inside a Rect THEN closestPointTo returns the same value`() {
        val rect =
            Rect(
                left = 0f,
                top = 0f,
                right = 100f,
                bottom = 100f,
            )
        val y = 50f
        assertEquals(y, rect.closestPointTo(y))
    }

    @Test
    fun `Given a y value is to the top of a rect THEN closestPointTo returns the top edge`() {
        val rect =
            Rect(
                left = 0f,
                top = 0f,
                right = 100f,
                bottom = 100f,
            )
        val y = rect.closestPointTo(-10f)
        assertEquals(y, rect.top)
    }

    @Test
    fun `Given a point is to the bottom of a rect THEN closestPointTo returns the bottom edge`() {
        val rect =
            Rect(
                left = 0f,
                top = 0f,
                right = 100f,
                bottom = 100f,
            )
        val y = rect.closestPointTo(110f)
        assertEquals(y, rect.bottom)
    }

    @Test
    fun `GIVEN a visible ListItem WHEN gatherCandidates is called THEN Overlap, Top and Bottom gutter candidates are created`() {
        val listState = mockListState(listOf(mockListItem("key"), mockListItem(TabKeys.TAB_BETA)))

        val candidates =
            gatherCandidates(
                listState = listState,
                draggedItemOffset = fakeDraggedListItemOffset(),
                draggedItem = fakeListActiveState(),
                ignoredItems = defaultIgnoredItems,
            )

        assertEquals(3, candidates.size)
        assertTrue(candidates.any { it.type is InteractionType.Overlap })
        assertTrue(candidates.any { it.type is InteractionType.TopGutter })
        assertTrue(candidates.any { it.type is InteractionType.BottomGutter })
    }

    @Test
    fun `GIVEN the first visible item is not the first in the list AND dragged item is at top of viewport WHEN gatherCandidates is called THEN top Scroll candidate is created`() {
        val listState =
            mockListState(
                listOf(
                    mockListItem("key"),
                    mockListItem(TabKeys.TAB_BETA),
                    mockListItem("key3"),
                ),
                firstVisibleIndex = 1,
            )

        val candidates =
            gatherCandidates(
                listState = listState,
                draggedItemOffset = fakeDraggedListItemOffset(),
                draggedItem = fakeListActiveState(),
                ignoredItems = defaultIgnoredItems,
            )

        assertEquals(1, candidates.count { it.type is InteractionType.Scroll })
    }

    @Test
    fun `GIVEN the last visible item is not the last in the list AND dragged item is at bottom of viewport WHEN gatherCandidates is called THEN bottom Scroll candidate is created`() {
        val listState =
            mockListState(
                listOf(
                    mockListItem("key"),
                    mockListItem(TabKeys.TAB_BETA),
                    mockListItem("key3"),
                ),
                firstVisibleIndex = 0,
                totalItems = 10,
            )

        val draggedItem =
            InteractionState.List.Active(
                index = 0,
                key = "key",
                size = 10,
                initialOffset = 10f,
            )
        val candidates =
            gatherCandidates(
                listState = listState,
                draggedItemOffset =
                    ListItemOffset(
                        draggedItem = draggedItem,
                        draggingItemOffset = 10f,
                        itemSize = 10,
                    ),
                draggedItem = draggedItem,
                ignoredItems = defaultIgnoredItems,
            )

        assertEquals(1, candidates.count { it.type is InteractionType.Scroll })
    }

    @Test
    fun `GIVEN an ignored ListItem THEN no candidates are generated`() {
        val listState = mockListState(mockItems = listOf(mockListItem("ignored")))

        val candidates =
            gatherCandidates(
                listState = listState,
                draggedItemOffset = fakeDraggedListItemOffset(),
                draggedItem = fakeListActiveState(),
                ignoredItems = setOf("ignored"),
            )

        assertTrue(candidates.isEmpty())
    }

    @Test
    fun `GIVEN an empty list THEN no candidates are generated`() {
        val listState = mockListState(mockItems = emptyList())

        val candidates =
            gatherCandidates(
                listState = listState,
                draggedItemOffset = fakeDraggedListItemOffset(),
                draggedItem = fakeListActiveState(),
                ignoredItems = defaultIgnoredItems,
            )

        assertTrue(candidates.isEmpty())
    }

    @Test
    fun `GIVEN liveReorderEnabled is false WHEN an item is dragged onto another WHEN onDragEnd is called THEN onDrop is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 120
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 140,
                includeHeader = true,
                liveReorderEnabled = false,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 20f, preserveSelectMode = false) // 20 down
        reorderState.onDragEnd()

        assertTrue(Call.Drop(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA) in handler.calls)
    }

    @Test
    fun `GIVEN liveReorderEnabled is true WHEN an item is dragged onto another WHEN onDragEnd is called THEN onDrop is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 120
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 140,
                includeHeader = true,
                liveReorderEnabled = true,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 20f, preserveSelectMode = false) // 20 down
        reorderState.onDragEnd()

        assertTrue(Call.Drop(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA) in handler.calls)
    }

    @Test
    fun `GIVEN drag and drop disabled and an item is dragged onto another WHEN onDragEnd is called THEN onDrop is not called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 120
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 140,
                includeHeader = true,
                dragAndDropEnabled = false,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 20f, preserveSelectMode = false) // 20 down
        reorderState.onDragEnd()

        assertFalse(Call.Drop(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA) in handler.calls)
        assertTrue(Call.Move(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA, false) in handler.calls)
    }

    @Test
    fun `GIVEN liveReorderEnabled is false WHEN an item is dragged to the bottom of another WHEN onDrag is called THEN onMove is not called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 110
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 130,
                includeHeader = true,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false) // 50 down

        assertTrue(handler.calls.none { it is Call.Move })
    }

    @Test
    fun `GIVEN liveReorderEnabled is true WHEN an item is dragged to the bottom of another WHEN onDrag is called THEN onMove is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 110
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 130,
                includeHeader = true,
                liveReorderEnabled = true,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false) // 50 down

        assertTrue(Call.Move(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA, true) in handler.calls)
    }

    @Test
    fun `GIVEN liveReorderEnabled is false WHEN an item is dragged to the bottom of another WHEN onDragEnd is called THEN onMove is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 110
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 130,
                includeHeader = true,
                liveReorderEnabled = false,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false) // 50 down
        reorderState.onDragEnd()

        assertTrue(Call.Move(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA, true) in handler.calls)
    }

    @Test
    fun `GIVEN liveReorderEnabled is false WHEN an item is dragged to the top of another WHEN onDrag is called THEN onMove is not called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 30
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = 10,
                betaTabOffset = dragItemOffset,
                liveReorderEnabled = false,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragUp(distance = 30f, preserveSelectMode = false) // 30 up

        assertTrue(handler.calls.none { it is Call.Move })
    }

    @Test
    fun `GIVEN liveReorderEnabled is true WHEN an item is dragged to the top of another WHEN onDrag is called THEN onMove is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 30
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = 10,
                betaTabOffset = dragItemOffset,
                liveReorderEnabled = true,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragUp(distance = 30f, preserveSelectMode = false) // 30 up

        assertTrue(Call.Move(TabKeys.TAB_BETA, TabKeys.TAB_ALPHA, false) in handler.calls)
    }

    @Test
    fun `GIVEN liveReorderEnabled is false WHEN an item is dragged to the top of another WHEN onDragEnd is called THEN onMove is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 30
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = 10,
                betaTabOffset = dragItemOffset,
                liveReorderEnabled = false,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragUp(distance = 30f, preserveSelectMode = false) // 30 up
        reorderState.onDragEnd()

        assertTrue(Call.Move(TabKeys.TAB_BETA, TabKeys.TAB_ALPHA, false) in handler.calls)
    }

    @Test
    fun `GIVEN liveReorderEnabled is true WHEN multiple drag events to the same position occur THEN only one onMove is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 30
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = 10,
                betaTabOffset = dragItemOffset,
                liveReorderEnabled = true,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragUp(distance = 30f, preserveSelectMode = false) // 30 up
        reorderState.dragDown(distance = 0f, preserveSelectMode = false)
        reorderState.dragDown(distance = 0f, preserveSelectMode = false)

        assertEquals(1, handler.calls.count { it == Call.Move(TabKeys.TAB_BETA, TabKeys.TAB_ALPHA, false) })
    }

    @Test
    fun `GIVEN a drag is in progress and the dragged item is not visible when onDragEnd is called THEN the state is reset`() {
        val reorderState = fakeListInteractionState(listState = mockListState(mockItems = emptyList()))

        reorderState.onTouchSlopPassed(0f, false)
        reorderState.onDragEnd()

        assertEquals(InteractionState.List.None, reorderState.draggedItem)
        assertEquals(InteractionState.List.None, reorderState.hoveredItem)
        assertNull(reorderState.highlightedRect)
        assertEquals(InteractionMode.List.None, reorderState.interactionMode)
    }

    @Test
    fun `GIVEN a drag is in progress and the dragged item is not visible when onDragEnd is called THEN onDragCancelled is called`() {
        val handler = RecordingItemInteractionHandler()
        val draggedItemOffset = 30
        val reorderState =
            fakeListInteractionState(
                mockListState(mockItems = listOf(mockListItem(key = TabKeys.TAB_BETA, offset = draggedItemOffset))),
                handler = handler,
            )

        reorderState.onTouchSlopPassed(draggedItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false)
        reorderState.onDragEnd()

        assertTrue(Call.DragCancel in handler.calls)
    }

    @Test
    fun `WHEN an item is dragged GIVEN preserveSelectMode is true THEN onDragStart is called with the same flag`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), true)
        reorderState.dragDown(distance = 50f, preserveSelectMode = true) // 50 down

        assertTrue(Call.DragStart(sourceKey = TabKeys.TAB_ALPHA, preserveSelectMode = true) in handler.calls)
    }

    @Test
    fun `WHEN an item is dragged GIVEN preserveSelectMode is false THEN onDragStart is called with the same flag`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), true)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false) // 50 down

        assertTrue(Call.DragStart(sourceKey = TabKeys.TAB_ALPHA, preserveSelectMode = false) in handler.calls)
    }

    @Test
    fun `WHEN a drag is cancelled THEN the handler is invoked with a drag cancel call`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
            )

        reorderState.onTouchSlopPassed(offset = dragItemOffset.toFloat(), shouldLongPress = false)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false)
        reorderState.onDragCancelled()

        assertTrue(Call.DragCancel in handler.calls)
    }

    @Test
    fun `WHEN a drag starts and the pointer does not move THEN the moved parameter is false`() {
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
            )

        reorderState.onTouchSlopPassed(offset = dragItemOffset.toFloat(), shouldLongPress = false)

        assertIs<InteractionState.List.Active>(reorderState.draggedItem)
        assertFalse(reorderState.draggedItem.moved)
    }

    @Test
    fun `WHEN a drag starts and the pointer moves THEN the moved parameter is true`() {
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
            )

        reorderState.onTouchSlopPassed(offset = dragItemOffset.toFloat(), shouldLongPress = false)
        reorderState.dragDown(distance = 40f, preserveSelectMode = true)

        assertIs<InteractionState.List.Active>(reorderState.draggedItem)
        assertTrue(reorderState.draggedItem.moved)
    }

    @Test
    fun `GIVEN an in progress drag WHEN onCancelled is called THEN the dragged item is reset to None and moved is false`() {
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
            )

        reorderState.onTouchSlopPassed(offset = dragItemOffset.toFloat(), shouldLongPress = false)
        reorderState.dragDown(distance = 40f, preserveSelectMode = true)
        reorderState.onDragCancelled()

        assertIs<InteractionState.List.None>(reorderState.draggedItem)
        assertFalse(reorderState.draggedItem.moved)
    }

    @Test
    fun `GIVEN an item with a non-String key WHEN it is dragged THEN the handler receives that key`() {
        val handler = RecordingItemInteractionHandler()
        val reorderState =
            fakeListInteractionState(
                mockListState(mockItems = listOf(mockListItem(key = 7, index = 0, offset = 0))),
                handler = handler,
            )

        reorderState.onTouchSlopPassed(offset = 1f, shouldLongPress = false)
        reorderState.onDrag(offset = 50f, preserveSelectMode = false)

        assertEquals(expected = 7, actual = reorderState.draggedItem.key)
        assertTrue(Call.DragStart(sourceKey = 7, preserveSelectMode = false) in handler.calls)
    }

    @Test
    fun `GIVEN an ignored item WHEN it is long pressed and dragged THEN no drag starts and onLongPress is not invoked`() {
        val handler = RecordingItemInteractionHandler()
        val longPressed = mutableListOf<Any>()
        val reorderState =
            fakeListInteractionState(
                mockListState(
                    mockItems =
                        listOf(
                            mockHeaderItem(),
                            mockListItem(key = TabKeys.TAB_ALPHA, index = 1, offset = 100),
                        )
                ),
                handler = handler,
                onLongPress = { longPressed += it.key },
            )

        reorderState.onTouchSlopPassed(offset = 50f, shouldLongPress = true)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false)

        assertIs<InteractionState.List.None>(reorderState.draggedItem)
        assertTrue(handler.calls.isEmpty())
        assertTrue(longPressed.isEmpty())
    }

    @Test
    fun `GIVEN an item which cannot be dragged WHEN it is long pressed and dragged THEN no drag starts and onLongPress is not invoked`() {
        val handler = RecordingItemInteractionHandler()
        val longPressed = mutableListOf<Any>()
        val dragItemOffset = 10
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 30,
                fixedItems = setOf(TabKeys.TAB_ALPHA),
                onLongPress = { longPressed += it.key },
            )

        reorderState.onTouchSlopPassed(offset = dragItemOffset.toFloat(), shouldLongPress = true)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false)

        assertIs<InteractionState.List.None>(reorderState.draggedItem)
        assertTrue(handler.calls.isEmpty())
        assertTrue(longPressed.isEmpty())
    }

    @Test
    fun `GIVEN an item which cannot be dragged WHEN another item is dragged to its top THEN the dragged item is moved before it`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 30
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = 10,
                betaTabOffset = dragItemOffset,
                liveReorderEnabled = true,
                fixedItems = setOf(TabKeys.TAB_ALPHA),
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragUp(distance = 30f, preserveSelectMode = false)

        assertTrue(Call.Move(TabKeys.TAB_BETA, TabKeys.TAB_ALPHA, false) in handler.calls)
    }

    @Test
    fun `GIVEN an item which cannot be dragged WHEN another item is dragged to its bottom THEN the dragged item is moved after it`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 110
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 130,
                includeHeader = true,
                liveReorderEnabled = true,
                fixedItems = setOf(TabKeys.TAB_BETA),
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 50f, preserveSelectMode = false)

        assertTrue(Call.Move(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA, true) in handler.calls)
    }

    @Test
    fun `GIVEN an item which cannot be dragged WHEN another item is dropped onto it THEN onDrop is called`() {
        val handler = RecordingItemInteractionHandler()
        val dragItemOffset = 120
        val reorderState =
            twoTabReorderState(
                handler = handler,
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 140,
                includeHeader = true,
                fixedItems = setOf(TabKeys.TAB_BETA),
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 20f, preserveSelectMode = false)
        reorderState.onDragEnd()

        assertTrue(Call.Drop(TabKeys.TAB_ALPHA, TabKeys.TAB_BETA) in handler.calls)
    }

    @Test
    fun `GIVEN items of different sizes WHEN a drag starts THEN the dragged item keeps its own size`() {
        val reorderState =
            fakeListInteractionState(
                mockListState(
                    mockItems =
                        listOf(
                            mockListItem(key = TabKeys.HEADER, index = 0, size = 100, offset = 0),
                            mockListItem(key = TabKeys.TAB_ALPHA, index = 1, size = 10, offset = 100),
                            mockListItem(key = TabKeys.TAB_BETA, index = 2, size = 40, offset = 110),
                        )
                )
            )

        reorderState.onTouchSlopPassed(offset = 130f, shouldLongPress = false)

        val draggedItem = assertIs<InteractionState.List.Active>(reorderState.draggedItem)
        assertEquals(expected = TabKeys.TAB_BETA, actual = draggedItem.key)
        assertEquals(expected = 40, actual = draggedItem.size)
    }

    @Test
    fun `GIVEN a hovered item WHEN the mode changes THEN the hovered item is reset`() {
        val dragItemOffset = 120
        val reorderState =
            twoTabReorderState(
                alphaTabOffset = dragItemOffset,
                betaTabOffset = 140,
                includeHeader = true,
                liveReorderEnabled = false,
            )

        reorderState.onTouchSlopPassed(dragItemOffset.toFloat(), false)
        reorderState.dragDown(distance = 20f, preserveSelectMode = false) // 20 down
        assertIs<InteractionState.List.Active>(reorderState.hoveredItem)

        reorderState.dragUp(distance = 20f, preserveSelectMode = false)

        assertIs<InteractionState.List.None>(reorderState.hoveredItem)
    }

    @Test
    fun `WHEN reset is called THEN the state is reset`() {
        val reorderState = fakeListInteractionState(listState = mockListState())
        reorderState.onTouchSlopPassed(0f, false)
        reorderState.onDragEnd()

        reorderState.reset()

        assertEquals(InteractionState.List.None, reorderState.draggedItem)
        assertEquals(InteractionState.List.None, reorderState.hoveredItem)
        assertNull(reorderState.highlightedRect)
        assertEquals(InteractionMode.List.None, reorderState.interactionMode)
        assertEquals(reorderState.previousItemAnimatableOffset.value, 0f)
    }

    private fun mockListItem(
        key: Any = "key",
        index: Int = 1,
        size: Int = 10,
        offset: Int = 0,
    ): LazyListItemInfo = FakeListItemInfo(key = key, index = index, size = size, offset = offset)

    private fun mockHeaderItem(): LazyListItemInfo =
        mockListItem(key = TabKeys.HEADER, index = 0, size = 100, offset = 0)

    private fun twoTabReorderState(
        handler: ItemInteractionHandler = NoOpItemInteractionHandler,
        alphaTabOffset: Int,
        betaTabOffset: Int,
        includeHeader: Boolean = false,
        dragAndDropEnabled: Boolean = true,
        liveReorderEnabled: Boolean = false,
        fixedItems: Set<Any> = emptySet(),
        onLongPress: (LazyListItemInfo) -> Unit = {},
    ): ListInteractionState {
        val items = buildList {
            if (includeHeader) {
                add(mockHeaderItem())
            }
            add(mockListItem(key = TabKeys.TAB_ALPHA, index = 1, offset = alphaTabOffset))
            add(mockListItem(key = TabKeys.TAB_BETA, index = 2, offset = betaTabOffset))
        }
        return fakeListInteractionState(
            mockListState(mockItems = items),
            handler = handler,
            dragAndDropEnabled = dragAndDropEnabled,
            liveReorderEnabled = liveReorderEnabled,
            fixedItems = fixedItems,
            onLongPress = onLongPress,
        )
    }

    private fun mockListState(
        mockItems: List<LazyListItemInfo> = emptyList(),
        firstVisibleIndex: Int = 0,
        totalItems: Int = mockItems.size,
    ): LazyListState {
        val listState = mock<LazyListState>()
        whenever(listState.firstVisibleItemIndex).thenReturn(firstVisibleIndex)
        whenever(listState.layoutInfo)
            .thenReturn(FakeListLayoutInfo(visibleItemsInfo = mockItems, totalItemsCount = totalItems))
        whenever(listState.isScrollInProgress).thenReturn(false)
        return listState
    }

    private fun fakeListInteractionState(
        listState: LazyListState,
        handler: ItemInteractionHandler = NoOpItemInteractionHandler,
        dragAndDropEnabled: Boolean = true,
        liveReorderEnabled: Boolean = false,
        onLongPress: (LazyListItemInfo) -> Unit = {},
        fixedItems: Set<Any> = emptySet(),
    ): ListInteractionState {
        return ListInteractionStateImpl(
            listState = listState,
            itemInteractionHandler = handler,
            scope = scope,
            touchSlop = 0f,
            ignoredItems = defaultIgnoredItems,
            onLongPress = onLongPress,
            hapticFeedback = RecordingHapticFeedback(),
            dragAndDropEnabled = dragAndDropEnabled,
            liveReorderEnabled = liveReorderEnabled,
            fixedItems = fixedItems,
        )
    }

    private fun ListInteractionState.dragDown(distance: Float, preserveSelectMode: Boolean) {
        this.onDrag(offset = distance, preserveSelectMode = preserveSelectMode)
    }

    private fun ListInteractionState.dragUp(distance: Float, preserveSelectMode: Boolean) {
        this.onDrag(offset = -distance, preserveSelectMode = preserveSelectMode)
    }
}
