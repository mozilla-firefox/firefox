/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListLayoutInfo
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent

/**
 * While a group is dragged, and until it is dropped in its place, draw the list item with [key] only while the list
 * shows it.
 *
 * The list keeps the dragged group laid out while it is scrolled out of view, before the items shown. Compose then
 * takes it for the first item shown, so an item still moving when it is scrolled out of view at the top is drawn moving
 * down across the list to its end instead. Not needed once https://issuetracker.google.com/issues/584383294 is fixed.
 *
 * @param key Key of the list item.
 * @param dragState State of dragging the groups of the screen.
 */
internal fun Modifier.drawnOnlyInView(key: Any, dragState: CustomizeMenuDragState): Modifier = drawWithContent {
    val interactionState = dragState.interactionState
    val listState = dragState.listState
    if (
        isDrawnInView(
            key = key,
            draggedKey = interactionState.draggedItem.key,
            droppedKey = interactionState.previousKeyOfDraggedItem,
            layoutInfo = { listState.layoutInfo },
            firstVisibleIndex = { listState.firstVisibleItemIndex },
        )
    ) {
        drawContent()
    }
}

/**
 * Whether the list item with [key] is drawn, see [drawnOnlyInView]. The list's layout is read only during a drag, so
 * outside one items are not redrawn on every scroll or relayout.
 *
 * @param key Key of the list item.
 * @param draggedKey Key of the dragged group, if any.
 * @param droppedKey Key of the group let go of, while it moves to its place.
 * @param layoutInfo Gives the layout of the list.
 * @param firstVisibleIndex Gives the index of the first item the list shows. Once the dragged group is let go of, items
 *   moving out of view are reported as shown, those at the top coming before this one.
 */
internal fun isDrawnInView(
    key: Any,
    draggedKey: Any?,
    droppedKey: Any?,
    layoutInfo: () -> LazyListLayoutInfo,
    firstVisibleIndex: () -> Int,
): Boolean {
    if (draggedKey == null && droppedKey == null) return true
    if (key == draggedKey || key == droppedKey) return true
    val first = firstVisibleIndex()
    return layoutInfo().visibleItemsInfo.any { it.key == key && it.index >= first }
}
