/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.grid.LazyGridItemInfo
import androidx.compose.foundation.lazy.grid.LazyGridLayoutInfo
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.unit.IntSize
import kotlin.test.Test
import kotlin.test.assertEquals
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertNull

class InteractionStateTest {

    @Test
    fun `GIVEN Grid None State WHEN incrementCumulatedOffset is called THEN None is returned unchanged`() {
        val result = InteractionState.Grid.None.incrementCumulatedOffset(Offset(10f, 10f))
        assertEquals(InteractionState.Grid.None, result)
    }

    @Test
    fun `GIVEN List None State WHEN incrementCumulatedOffset is called THEN None is returned unchanged`() {
        val result = InteractionState.List.None.incrementCumulatedOffset(10f)
        assertEquals(InteractionState.List.None, result)
    }

    @Test
    fun `GIVEN Grid Active State with initial offset Zero WHEN incrementCumulatedOffset is called THEN State is updated`() {
        val initialState =
            InteractionState.Grid.Active(index = 0, key = "key", size = IntSize(10, 10), initialOffset = Offset.Zero)
        val result = initialState.incrementCumulatedOffset(Offset(10f, 10f))
        assertEquals(result.cumulatedOffset, Offset(10f, 10f))
    }

    @Test
    fun `GIVEN List Active State with initial offset Zero WHEN incrementCumulatedOffset is called THEN State is updated`() {
        val initialState = InteractionState.List.Active(index = 0, key = "key", size = 10, initialOffset = 0f)
        val result = initialState.incrementCumulatedOffset(10f)
        assertEquals(result.cumulatedOffset, 10f)
    }

    @Test
    fun `GIVEN Grid Active State WHEN incrementCumulatedOffset is called THEN State is accumulated`() {
        val initialState =
            InteractionState.Grid.Active(
                index = 0,
                key = "key",
                size = IntSize(10, 10),
                initialOffset = Offset(10f, 10f),
            )
        val result =
            initialState
                .incrementCumulatedOffset(Offset(10f, 10f))
                .incrementCumulatedOffset(Offset(10f, 10f))
                .incrementCumulatedOffset(Offset(10f, 10f))
        assertEquals(Offset(30f, 30f), result.cumulatedOffset)
    }

    @Test
    fun `GIVEN List Active State WHEN incrementCumulatedOffset is called THEN State is accumulated`() {
        val initialState = InteractionState.List.Active(index = 0, key = "key", size = 10, initialOffset = 10f)
        val result =
            initialState.incrementCumulatedOffset(10f).incrementCumulatedOffset(10f).incrementCumulatedOffset(10f)
        assertEquals(30f, result.cumulatedOffset)
    }

    @Test
    fun `GIVEN Grid Active State WHEN its offset or index changes THEN its size is preserved`() {
        val initialState = InteractionState.Grid.Active(index = 0, key = "key", size = IntSize(600, 750))
        val result = initialState.incrementCumulatedOffset(Offset(10f, 10f)).copyWithNewIndex(3)
        assertEquals(IntSize(600, 750), (result as InteractionState.Grid.Active).size)
    }

    @Test
    fun `GIVEN List Active State WHEN its offset or moved flag changes THEN its size is preserved`() {
        val initialState = InteractionState.List.Active(index = 0, key = "key", size = 750)
        val result = initialState.incrementCumulatedOffset(10f).markAsMoved()
        assertEquals(750, (result as InteractionState.List.Active).size)
    }

    @Test
    fun `GIVEN Grid None state when lazyGridItemInfo is called THEN null is returned`() {
        val mockItem = mock<LazyGridItemInfo>()
        val gridState = mockGridState(mockItem)
        assertNull(InteractionState.Grid.None.getLazyGridItemInfo(gridState))
    }

    @Test
    fun `GIVEN List None state when lazyListItemInfo is called THEN null is returned`() {
        val listState = mockListState(FakeListItemInfo(key = "key", index = 0, size = 10, offset = 0))
        assertNull(InteractionState.List.None.getLazyListItemInfo(listState))
    }

    @Test
    fun `Given Grid Active state when lazyGridItemInfo is called with a matching item THEN that item is returned`() {
        val mockItem = mockGridItem(key = "key", index = 0)
        val gridState = mockGridState(mockItem)
        val result =
            InteractionState.Grid.Active(index = 0, key = "key", size = IntSize(10, 10), initialOffset = Offset.Zero)
                .getLazyGridItemInfo(gridState)
        assertEquals(mockItem, result)
    }

    @Test
    fun `Given List Active state when lazyListItemInfo is called with a matching item THEN that item is returned`() {
        val item = FakeListItemInfo(key = "key", index = 0, size = 10, offset = 0)
        val listState = mockListState(item)
        val result =
            InteractionState.List.Active(index = 0, key = "key", size = 10, initialOffset = 0f)
                .getLazyListItemInfo(listState)
        assertEquals(item, result)
    }

    @Test
    fun `Given Grid Active state when lazyGridItemInfo is called without a matching item THEN null is returned`() {
        val mockItem = mockGridItem(key = "otherKey", index = 1)
        val gridState = mockGridState(mockItem)
        val result =
            InteractionState.Grid.Active(index = 0, key = "key", size = IntSize(10, 10), initialOffset = Offset.Zero)
                .getLazyGridItemInfo(gridState)
        assertNull(result)
    }

    @Test
    fun `Given List Active state when lazyListItemInfo is called without a matching item THEN null is returned`() {
        val listState = mockListState(FakeListItemInfo(key = "otherKey", index = 1, size = 10, offset = 0))
        val result =
            InteractionState.List.Active(index = 0, key = "key", size = 10, initialOffset = 0f)
                .getLazyListItemInfo(listState)
        assertNull(result)
    }

    private fun mockGridItem(key: String, index: Int): LazyGridItemInfo {
        val item = mock<LazyGridItemInfo>()
        whenever(item.key).thenReturn(key)
        whenever(item.index).thenReturn(index)
        return item
    }

    private fun mockGridState(item: LazyGridItemInfo): LazyGridState {
        val layoutInfo = mock<LazyGridLayoutInfo>()
        whenever(layoutInfo.visibleItemsInfo).thenReturn(listOf(item))
        val gridState = mock<LazyGridState>()
        whenever(gridState.layoutInfo).thenReturn(layoutInfo)
        return gridState
    }

    private fun mockListState(item: LazyListItemInfo): LazyListState {
        val listState = mock<LazyListState>()
        whenever(listState.layoutInfo)
            .thenReturn(FakeListLayoutInfo(visibleItemsInfo = listOf(item), totalItemsCount = 1))
        return listState
    }
}
