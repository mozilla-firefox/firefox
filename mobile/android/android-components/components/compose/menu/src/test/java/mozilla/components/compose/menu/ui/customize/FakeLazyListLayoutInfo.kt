/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListLayoutInfo
import androidx.compose.ui.unit.IntSize
import mozilla.components.compose.draggable.InteractionState

/** Size of the items of the lists laid out by [layoutOf]. */
internal const val ITEM_SIZE = 100

/** The group with [id], dragged with its center at [center]. */
internal fun dragged(id: String, center: Float) =
    InteractionState.List.Active(index = 1, key = id, size = ITEM_SIZE, initialOffset = center - ITEM_SIZE / 2)

/**
 * Layout of a list showing the items with [keys] one after another from its top, the first being the item at
 * [firstIndex] of the [totalItemsCount] items of the list. The item with the [collapsed] key takes no space, as a
 * dragged group does.
 */
internal fun layoutOf(
    vararg keys: Any,
    firstIndex: Int = 0,
    totalItemsCount: Int = firstIndex + keys.size,
    collapsed: Any? = null,
): LazyListLayoutInfo {
    var offset = 0
    return FakeLayoutInfo(
        visibleItemsInfo =
            keys.mapIndexed { position, key ->
                val size = if (key == collapsed) 0 else ITEM_SIZE
                FakeItemInfo(index = firstIndex + position, key = key, offset = offset, size = size).also {
                    offset += size
                }
            },
        totalItemsCount = totalItemsCount,
    )
}

private data class FakeItemInfo(
    override val index: Int,
    override val key: Any,
    override val offset: Int,
    override val size: Int,
) : LazyListItemInfo

private data class FakeLayoutInfo(
    override val visibleItemsInfo: List<LazyListItemInfo>,
    override val totalItemsCount: Int,
    override val viewportStartOffset: Int = 0,
    override val viewportEndOffset: Int = 2_000,
    override val viewportSize: IntSize = IntSize(100, 2_000),
) : LazyListLayoutInfo
