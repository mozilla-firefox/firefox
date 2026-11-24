/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListLayoutInfo
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON

/** Where a dragged group or item is relative to the area holding the hidden ones. */
internal enum class DraggedPosition {
    ABOVE_HIDDEN_AREA,
    OVER_HIDDEN_AREA,

    /** Letting go of the dragged one here puts it back to where it was. */
    BELOW_HIDDEN_AREA,
}

/**
 * Where [dragged] is relative to the area holding the hidden groups or items: below the title above them, until the end
 * of the area to drag into, the reset button or the last hidden one, as laid out in the list.
 *
 * @param dragged The dragged group or item.
 * @param layoutInfo Layout of the list showing the groups or items.
 * @param items The groups or items as they were before dragging.
 */
internal fun draggedPositionOf(
    dragged: InteractionState.List.Active,
    layoutInfo: LazyListLayoutInfo,
    items: HideableListItems,
): DraggedPosition {
    // Whether hidden ones are shown below the title instead of the area to drag into. Dragged out of them, the last
    // hidden one is still laid out below that area.
    val showsHiddenItems = items.hiddenKeys.isNotEmpty()
    val draggedCenter = dragged.initialOffset + dragged.cumulatedOffset + dragged.size / 2f
    val visibleItems = layoutInfo.visibleItemsInfo
    val title = visibleItems.firstOrNull { it.key == HIDDEN_TITLE }
    // When not shown the title is below or above everything shown.
    val top =
        when {
            title != null -> title.endOffset
            (visibleItems.firstOrNull()?.index ?: 0) < items.hiddenTitleIndex -> Float.POSITIVE_INFINITY
            else -> Float.NEGATIVE_INFINITY
        }
    val bottom =
        when {
            !showsHiddenItems ->
                visibleItems.firstOrNull { it.key == DROP_AREA }?.endOffset
                    ?: if (title != null) Float.POSITIVE_INFINITY else Float.NEGATIVE_INFINITY
            else ->
                visibleItems.firstOrNull { it.key == RESET_BUTTON }?.offset?.toFloat()
                    // Without a reset button, the area ends with the last hidden one, if shown.
                    ?: visibleItems.lastOrNull()?.takeIf { it.index == layoutInfo.totalItemsCount - 1 }?.endOffset
                    ?: Float.POSITIVE_INFINITY
        }

    return when {
        draggedCenter <= top -> DraggedPosition.ABOVE_HIDDEN_AREA
        draggedCenter <= bottom -> DraggedPosition.OVER_HIDDEN_AREA
        else -> DraggedPosition.BELOW_HIDDEN_AREA
    }
}

private val LazyListItemInfo.endOffset: Float
    get() = (offset + size).toFloat()
