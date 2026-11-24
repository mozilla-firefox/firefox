/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Dp

/**
 * Lay out a list item without taking any space while it is dragged, for the other items to take its space, showing
 * there if it is dropped back.
 *
 * @param isDragged Whether the item is dragged.
 * @param dropTarget Where the dragged item is dropped.
 * @param itemSpacing Space above each item of the list, which the dragged item takes too.
 */
@Composable
internal fun Modifier.dragPlacement(
    isDragged: Boolean,
    dropTarget: State<CustomizeMenuDropTarget?>,
    itemSpacing: Dp,
): Modifier = this.then(if (isDragged) Modifier.collapsed().inPlaceDropLine(dropTarget, itemSpacing) else Modifier)

/** Lay out without taking any space, drawing over what is laid out next. */
private fun Modifier.collapsed() = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    layout(placeable.width, 0) { placeable.place(0, 0) }
}
