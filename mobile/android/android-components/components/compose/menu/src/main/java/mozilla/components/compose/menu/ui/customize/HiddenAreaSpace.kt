/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.utils.HIDDEN_AREA_SPACE_PART

/**
 * Height of the empty space keeping the hidden items in place while a shown item is dragged and the shown items below
 * it close the gap it leaves: the dragged item's size, less the drop line's space if the line is among the shown items,
 * last among them included, and moves the items below it.
 *
 * @param dragged The dragged item, if it was among the shown items.
 * @param lineSpace Space the drop line takes.
 * @param dropTarget Where the dragged item is dropped.
 * @param items The items as they were before dragging.
 * @param firstVisibleIndex Index of the first item the list shows, read only if the item is dropped back where it was.
 *   Its line moves no other item if it is above that item, as the list keeps that one in place.
 */
internal fun hiddenAreaFillerOf(
    dragged: DraggedShownItem?,
    lineSpace: Int,
    dropTarget: CustomizeMenuDropTarget?,
    items: HideableListItems,
    firstVisibleIndex: () -> Int,
): Int {
    if (dragged == null) return 0
    val lineMovesHiddenSections =
        when (dropTarget) {
            is CustomizeMenuDropTarget.NextTo ->
                if (dropTarget.key == HIDDEN_TITLE) {
                    !dropTarget.placeAfter
                } else {
                    items.isShownKey(dropTarget.key)
                }
            CustomizeMenuDropTarget.InPlace -> dragged.index >= firstVisibleIndex()
            CustomizeMenuDropTarget.EmptyHiddenArea,
            null -> false
        }
    return if (lineMovesHiddenSections) (dragged.size - lineSpace).coerceAtLeast(0) else dragged.size
}

/**
 * Empty space above the hidden items title, keeping the hidden items in place while a shown item is dragged, as found
 * by [hiddenAreaFillerOf]. The drop line is shown at its top if the dragged item is dropped last among the shown items.
 * It is a node of its own, apart from the title, so that the title does not move when this changes size.
 *
 * @param dragState State of dragging the items of the screen.
 * @param itemSpacing Space between the items of the list.
 * @param modifier The [Modifier] to be applied to the space.
 */
@Composable
internal fun HiddenAreaSpace(dragState: CustomizeMenuDragState<*>, itemSpacing: Dp, modifier: Modifier = Modifier) {
    val lineThickness = AcornTheme.layout.border.heaviest
    fun hasLine() =
        dropLineSpaceOf(key = HIDDEN_TITLE, isDragged = false, dropTarget = dragState.dropTarget.value) ==
            DropLineSpace.ABOVE
    Spacer(
        modifier
            .fillMaxWidth()
            .dropLineAt(itemSpacing) { if (hasLine()) DropLineSpace.ABOVE else null }
            .layout { measurable, constraints ->
                val lineSpace = dropLineSpacePx(itemSpacing = itemSpacing, thickness = lineThickness)
                val line = if (hasLine()) lineSpace else 0
                val dragged = dragState.draggedShownItem.value
                val filler =
                    hiddenAreaFillerOf(
                        dragged = dragged,
                        lineSpace = lineSpace,
                        dropTarget = dragState.dropTarget.value,
                        items = dragState.items,
                        firstVisibleIndex = { dragState.listState.firstVisibleItemIndex },
                    )
                val height = line + filler
                // To find where the dragged group is dropped, the filler is kept, for the hidden sections to be found
                // where they are shown. Only the line's space is left out, so the filler made smaller for a line among
                // the shown groups counts as negative extra space, making up for that line's space.
                dragState.extraSpace.record(
                    key = HIDDEN_TITLE,
                    extra = line - ((dragged?.size ?: 0) - filler),
                    size = height,
                    part = HIDDEN_AREA_SPACE_PART,
                )
                val placeable = measurable.measure(constraints.copy(minHeight = height, maxHeight = height))
                layout(placeable.width, height) { placeable.place(0, 0) }
            }
    )
}
