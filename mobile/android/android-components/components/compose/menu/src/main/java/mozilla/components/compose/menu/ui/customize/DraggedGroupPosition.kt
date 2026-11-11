/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListLayoutInfo
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON

/** Where a dragged group is relative to the area holding the hidden groups. */
internal enum class DraggedGroupPosition {
    ABOVE_HIDDEN_AREA,
    OVER_HIDDEN_AREA,

    /** Letting go of the group here puts it back to where it was. */
    BELOW_HIDDEN_AREA,
}

/**
 * Where [dragged] is relative to the area holding the hidden groups: below the hidden sections title, until the end of
 * the area to drag groups into, the reset button or the last hidden group, as laid out in the list.
 *
 * @param dragged The dragged group.
 * @param layoutInfo Layout of the list showing the groups.
 * @param groups The groups as they were before dragging.
 */
internal fun draggedGroupPositionOf(
    dragged: InteractionState.List.Active,
    layoutInfo: LazyListLayoutInfo,
    groups: CustomizeMenuGroups,
): DraggedGroupPosition {
    // Whether hidden groups are shown below the title instead of the area to drag groups into. Dragged out of them,
    // the last hidden group is still laid out below that area.
    val showsHiddenGroups = groups.hidden.isNotEmpty()
    val draggedCenter = dragged.initialOffset + dragged.cumulatedOffset + dragged.size / 2f
    val visibleItems = layoutInfo.visibleItemsInfo
    val title = visibleItems.firstOrNull { it.key == HIDDEN_SECTIONS }
    // When not shown the title is below or above everything shown.
    val top =
        when {
            title != null -> title.endOffset
            (visibleItems.firstOrNull()?.index ?: 0) < groups.hiddenSectionsIndex -> Float.POSITIVE_INFINITY
            else -> Float.NEGATIVE_INFINITY
        }
    val bottom =
        when {
            !showsHiddenGroups ->
                visibleItems.firstOrNull { it.key == DROP_AREA }?.endOffset
                    ?: if (title != null) Float.POSITIVE_INFINITY else Float.NEGATIVE_INFINITY
            else ->
                visibleItems.firstOrNull { it.key == RESET_BUTTON }?.offset?.toFloat()
                    // Without a reset button, the area ends with the last hidden group, if shown.
                    ?: visibleItems.lastOrNull()?.takeIf { it.index == layoutInfo.totalItemsCount - 1 }?.endOffset
                    ?: Float.POSITIVE_INFINITY
        }

    return when {
        draggedCenter <= top -> DraggedGroupPosition.ABOVE_HIDDEN_AREA
        draggedCenter <= bottom -> DraggedGroupPosition.OVER_HIDDEN_AREA
        else -> DraggedGroupPosition.BELOW_HIDDEN_AREA
    }
}

private val LazyListItemInfo.endOffset: Float
    get() = (offset + size).toFloat()
