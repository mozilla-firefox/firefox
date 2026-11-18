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
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS

/**
 * Empty space above the hidden sections title, keeping the hidden sections in place while a shown group is dragged and
 * the shown groups below it close the gap it leaves. It takes the dragged group's size. The drop line is shown at its
 * top if the dragged group is dropped last among the shown groups. It is a node of its own, apart from the title, so
 * that the title does not move when this changes size.
 *
 * @param dragState State of dragging the groups of the screen.
 * @param itemSpacing Space between the items of the list.
 * @param modifier The [Modifier] to be applied to the space.
 */
@Composable
internal fun HiddenAreaSpace(dragState: CustomizeMenuDragState, itemSpacing: Dp, modifier: Modifier = Modifier) {
    Spacer(
        modifier
            .fillMaxWidth()
            .dropLineAt {
                val target = dragState.dropTarget.value as? CustomizeMenuDropTarget.NextTo
                if (target?.key == HIDDEN_SECTIONS && !target.placeAfter) itemSpacing.toPx() / 2 else null
            }
            .layout { measurable, constraints ->
                val height = dragState.draggedShownGroup.value?.size ?: 0
                val placeable = measurable.measure(constraints.copy(minHeight = height, maxHeight = height))
                layout(placeable.width, height) { placeable.place(0, 0) }
            }
    )
}
