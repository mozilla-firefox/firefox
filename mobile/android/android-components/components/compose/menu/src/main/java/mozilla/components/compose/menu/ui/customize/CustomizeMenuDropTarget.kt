/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.unit.Dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.InteractionMode
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS

/** Where a group of the customize menu screen is moved to if the user lets go of it. */
internal sealed interface CustomizeMenuDropTarget {
    /**
     * Next to the list item with [key].
     *
     * @property key Key of the list item: the id of a group or [HIDDEN_SECTIONS], to show the group last or hide it
     *   first.
     * @property placeAfter Whether the group is placed after the item instead of before it.
     */
    data class NextTo(val key: Any, val placeAfter: Boolean) : CustomizeMenuDropTarget

    /** Into the empty area to drag groups into to hide them. */
    data object EmptyHiddenArea : CustomizeMenuDropTarget

    /** Back where the group was taken from. */
    data object InPlace : CustomizeMenuDropTarget
}

/**
 * Where a dragged group is dropped, if anywhere: where the list would place it, but hidden only if dragged over the
 * area holding the hidden groups and if the group is not mandatory.
 *
 * @param draggedGroupId Id of the dragged group.
 * @param reordering Where the list would place the dragged group, if anywhere.
 * @param position Where the dragged group is relative to the area holding the hidden groups.
 * @param groups The groups as they were before dragging.
 * @param hasHiddenGroups Whether groups other than the dragged one are hidden.
 */
internal fun dropTargetOf(
    draggedGroupId: Any,
    reordering: InteractionMode.List.Reordering?,
    position: DraggedGroupPosition?,
    groups: CustomizeMenuGroups,
    hasHiddenGroups: Boolean,
): CustomizeMenuDropTarget? {
    val isOverHiddenArea = position == DraggedGroupPosition.OVER_HIDDEN_AREA
    return when {
        position == null || position == DraggedGroupPosition.BELOW_HIDDEN_AREA -> null
        isOverHiddenArea && draggedGroupId in groups.mandatoryIds -> null
        isOverHiddenArea && !hasHiddenGroups -> CustomizeMenuDropTarget.EmptyHiddenArea
        reordering == null -> null
        else -> nextToTargetOf(draggedGroupId, reordering, isOverHiddenArea, groups)
    }
}

/**
 * Where the list would place the dragged group, but hidden only if dragged over the area holding the hidden groups.
 * Next to the items around it, the group is dropped where it was taken from.
 *
 * @param draggedGroupId Id of the dragged group.
 * @param reordering Where the list would place the dragged group.
 * @param isOverHiddenArea Whether the dragged group is over the area holding the hidden groups, otherwise above it.
 * @param groups The groups as they were before dragging.
 */
private fun nextToTargetOf(
    draggedGroupId: Any,
    reordering: InteractionMode.List.Reordering,
    isOverHiddenArea: Boolean,
    groups: CustomizeMenuGroups,
): CustomizeMenuDropTarget {
    val targetKey = reordering.target.key
    val hides = (targetKey == HIDDEN_SECTIONS && reordering.placeAfter) || groups.hidden.any { it.id == targetKey }
    val target =
        when {
            !isOverHiddenArea && hides -> CustomizeMenuDropTarget.NextTo(HIDDEN_SECTIONS, placeAfter = false)
            isOverHiddenArea && !hides -> CustomizeMenuDropTarget.NextTo(HIDDEN_SECTIONS, placeAfter = true)
            else -> CustomizeMenuDropTarget.NextTo(targetKey, reordering.placeAfter)
        }
    return if (groups.drop(draggedGroupId, target) == groups) CustomizeMenuDropTarget.InPlace else target
}

/** Move the group with [groupId] to [target]. */
internal fun CustomizeMenuGroups.drop(groupId: Any, target: CustomizeMenuDropTarget): CustomizeMenuGroups =
    when (target) {
        is CustomizeMenuDropTarget.NextTo -> moveGroup(groupId, target.key, target.placeAfter)
        CustomizeMenuDropTarget.EmptyHiddenArea -> moveGroup(groupId, HIDDEN_SECTIONS, placeAfter = true)
        CustomizeMenuDropTarget.InPlace -> this
    }

/**
 * Draw a line in the space between list items where a dragged group is dropped, if next to the list item with [key].
 *
 * @param key Key of the list item this is applied to.
 * @param dropTarget Where the dragged group is dropped, read only when drawing.
 * @param itemSpacing Space between the items of the list.
 */
@Composable
internal fun Modifier.dropLine(key: Any, dropTarget: State<CustomizeMenuDropTarget?>, itemSpacing: Dp): Modifier =
    dropLineAt {
        val target = dropTarget.value as? CustomizeMenuDropTarget.NextTo
        when {
            target?.key != key -> null
            target.placeAfter -> size.height + itemSpacing.toPx() / 2
            else -> -itemSpacing.toPx() / 2
        }
    }

/**
 * Draw a line in the space between the items around where the dragged group was taken from, if dropped there. Applied
 * to where the dragged group is laid out, without taking any space, so the space above the item below it.
 *
 * @param dropTarget Where the dragged group is dropped, read only when drawing.
 * @param itemSpacing Space between the items of the list.
 */
@Composable
internal fun Modifier.inPlaceDropLine(dropTarget: State<CustomizeMenuDropTarget?>, itemSpacing: Dp): Modifier =
    dropLineAt {
        if (dropTarget.value == CustomizeMenuDropTarget.InPlace) itemSpacing.toPx() / 2 else null
    }

/** Draw a line at the vertical offset returned by [lineY], if any. */
@Composable
private fun Modifier.dropLineAt(lineY: DrawScope.() -> Float?): Modifier {
    val color = MaterialTheme.colorScheme.tertiary
    val strokeWidth = AcornTheme.layout.border.heaviest
    val horizontalInset = AcornTheme.layout.space.static200
    return drawBehind {
        val y = lineY() ?: return@drawBehind
        drawLine(
            color = color,
            start = Offset(horizontalInset.toPx(), y),
            end = Offset(size.width - horizontalInset.toPx(), y),
            strokeWidth = strokeWidth.toPx(),
            cap = StrokeCap.Round,
        )
    }
}
