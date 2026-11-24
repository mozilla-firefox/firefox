/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Dp

/**
 * While the item is dragged, it takes no space, so the other items close the gap. It only takes space for the drop
 * line, when it would be dropped back where it was.
 *
 * @param key Key of the list item.
 * @param isDragged Whether the item is dragged.
 * @param dragState State of dragging the items of the screen.
 * @param itemSpacing Space above each item of the list.
 */
@Composable
internal fun Modifier.dragPlacement(
    key: Any,
    isDragged: Boolean,
    dragState: CustomizeMenuDragState<*>,
    itemSpacing: Dp,
): Modifier =
    this.dropLineSpace(
            key = key,
            isDragged = isDragged,
            dropTarget = dragState.dropTarget,
            extraSpace = dragState.extraSpace,
            itemSpacing = itemSpacing,
        )
        .then(if (isDragged) Modifier.collapsed() else Modifier)

/** Lay out without taking any space, drawing over what is laid out next. */
private fun Modifier.collapsed() = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    layout(placeable.width, 0) { placeable.place(0, 0) }
}
