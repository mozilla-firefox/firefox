/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListLayoutInfo
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
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
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
 * The interaction to use for the [dragged] group instead of [mode], the one the list found for it: placing it where the
 * list would, but hidden only if dragged over the area holding the hidden groups and if the group is not mandatory,
 * into the empty area to drag groups into if dragged over it, and nowhere if dragged below the hidden groups.
 *
 * @param dragged The dragged group.
 * @param mode The interaction the list found for the dragged group.
 * @param layoutInfo Layout of the list showing the groups.
 * @param groups The groups as they were before dragging.
 */
internal fun interactionModeOf(
    dragged: InteractionState.List.Active,
    mode: InteractionMode.List,
    layoutInfo: LazyListLayoutInfo,
    groups: CustomizeMenuGroups,
): InteractionMode.List {
    // Until the list lays out the dragged group taking no space, the other groups are not yet where they move to.
    // The dragged group must then be laid out with a size of exactly 0, as by `collapsed` in CustomizeMenu.kt,
    // or it is never dropped anywhere.
    if (layoutInfo.visibleItemsInfo.any { it.key == dragged.key && it.size > 0 }) return InteractionMode.List.None
    val position = draggedGroupPositionOf(dragged, layoutInfo, groups)
    val isOverHiddenArea = position == DraggedGroupPosition.OVER_HIDDEN_AREA
    val hasOtherHiddenGroups = groups.hidden.any { it.id != dragged.key }
    return when {
        position == DraggedGroupPosition.BELOW_HIDDEN_AREA -> InteractionMode.List.None
        isOverHiddenArea && dragged.key in groups.mandatoryIds -> InteractionMode.List.None
        isOverHiddenArea && !hasOtherHiddenGroups -> dragged.placedNextTo(DROP_AREA, placeAfter = true, layoutInfo)
        mode !is InteractionMode.List.Reordering -> mode
        else -> mode.retargeted(isOverHiddenArea, groups, layoutInfo)
    }
}

/**
 * Where the list would place the dragged group, but hidden only if dragged over the area holding the hidden groups.
 *
 * @param isOverHiddenArea Whether the dragged group is over the area holding the hidden groups, otherwise above it.
 * @param groups The groups as they were before dragging.
 * @param layoutInfo Layout of the list showing the groups.
 */
private fun InteractionMode.List.Reordering.retargeted(
    isOverHiddenArea: Boolean,
    groups: CustomizeMenuGroups,
    layoutInfo: LazyListLayoutInfo,
): InteractionMode.List {
    val targetKey = target.key
    val hides =
        targetKey == DROP_AREA ||
            (targetKey == HIDDEN_SECTIONS && placeAfter) ||
            groups.hidden.any { it.id == targetKey }
    return when {
        !isOverHiddenArea && hides -> source.placedNextTo(HIDDEN_SECTIONS, placeAfter = false, layoutInfo)
        isOverHiddenArea && !hides -> source.placedNextTo(HIDDEN_SECTIONS, placeAfter = true, layoutInfo)
        else -> this
    }
}

/** Place this next to the list item with [key], if it is laid out. */
private fun InteractionState.List.Active.placedNextTo(
    key: Any,
    placeAfter: Boolean,
    layoutInfo: LazyListLayoutInfo,
): InteractionMode.List {
    val item = layoutInfo.visibleItemsInfo.firstOrNull { it.key == key } ?: return InteractionMode.List.None
    return InteractionMode.List.Reordering(
        source = this,
        target =
            InteractionState.List.Active(
                index = item.index,
                key = item.key,
                size = item.size,
                initialOffset = item.offset.toFloat(),
            ),
        placeAfter = placeAfter,
    )
}

/**
 * Where the group with [draggedGroupId] is dropped if the user lets go of it during [mode], if anywhere. Next to the
 * items around it, the group is dropped where it was taken from.
 *
 * @param draggedGroupId Id of the dragged group.
 * @param mode The interaction for the dragged group.
 * @param groups The groups as they were before dragging.
 */
internal fun dropTargetOf(
    draggedGroupId: Any,
    mode: InteractionMode.List,
    groups: CustomizeMenuGroups,
): CustomizeMenuDropTarget? {
    val reordering = mode as? InteractionMode.List.Reordering ?: return null
    if (reordering.target.key == DROP_AREA) return CustomizeMenuDropTarget.EmptyHiddenArea
    val target = CustomizeMenuDropTarget.NextTo(reordering.target.key, reordering.placeAfter)
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
