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
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.constrainHeight
import androidx.compose.ui.unit.offset
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.InteractionMode
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE

/** Where a group or an item of a customize menu screen is moved to if the user lets go of it. */
internal sealed interface CustomizeMenuDropTarget {
    /**
     * Next to the list item with [key].
     *
     * @property key Key of the list item: the id of a group or an item, or [HIDDEN_TITLE], to show the dragged one last
     *   or hide it first.
     * @property placeAfter Whether the dragged group or item is placed after the list item instead of before it.
     */
    data class NextTo(val key: Any, val placeAfter: Boolean) : CustomizeMenuDropTarget

    /** Into the empty area to drag groups or items into to hide them. */
    data object EmptyHiddenArea : CustomizeMenuDropTarget

    /** Back where the dragged group or item was taken from. */
    data object InPlace : CustomizeMenuDropTarget
}

/**
 * The interaction to use for the [dragged] group or item instead of [mode], the one the list found for it: placing it
 * where the list would, but hidden only if dragged over the area holding the hidden ones and if it can be hidden, into
 * the empty area to drag into if dragged over it, and nowhere if dragged below the hidden ones.
 *
 * @param dragged The dragged group or item.
 * @param mode The interaction the list found for the dragged group or item.
 * @param layoutInfo Layout of the list showing the groups or items, leaving out the drop line's space.
 * @param items The groups or items as they were before dragging.
 */
internal fun interactionModeOf(
    dragged: InteractionState.List.Active,
    mode: InteractionMode.List,
    layoutInfo: LazyListLayoutInfo,
    items: HideableListItems,
): InteractionMode.List {
    // Until the list lays out the dragged one taking no space, the others are not yet where they move to.
    // The dragged one must then take exactly no space (see `collapsed` in CustomizeMenu.kt), not counting the drop
    // line, or it is never dropped anywhere.
    if (layoutInfo.visibleItemsInfo.any { it.key == dragged.key && it.size > 0 }) return InteractionMode.List.None
    val position = draggedPositionOf(dragged, layoutInfo, items)
    val isOverHiddenArea = position == DraggedPosition.OVER_HIDDEN_AREA
    val hasOtherHiddenItems = items.hiddenKeys.any { it != dragged.key }
    return when {
        position == DraggedPosition.BELOW_HIDDEN_AREA -> InteractionMode.List.None
        isOverHiddenArea && items.cannotBeHidden(dragged.key) -> InteractionMode.List.None
        isOverHiddenArea && !hasOtherHiddenItems -> dragged.placedNextTo(DROP_AREA, placeAfter = true, layoutInfo)
        mode !is InteractionMode.List.Reordering -> mode
        else -> mode.retargeted(isOverHiddenArea, items, layoutInfo)
    }
}

/**
 * Where the list would place the dragged group or item, but hidden only if dragged over the area holding the hidden
 * ones.
 *
 * @param isOverHiddenArea Whether the dragged one is over the area holding the hidden ones, otherwise above it.
 * @param items The groups or items as they were before dragging.
 * @param layoutInfo Layout of the list showing the groups or items.
 */
private fun InteractionMode.List.Reordering.retargeted(
    isOverHiddenArea: Boolean,
    items: HideableListItems,
    layoutInfo: LazyListLayoutInfo,
): InteractionMode.List {
    val targetKey = target.key
    val hides = targetKey == DROP_AREA || (targetKey == HIDDEN_TITLE && placeAfter) || targetKey in items.hiddenKeys
    return when {
        !isOverHiddenArea && hides -> source.placedNextTo(HIDDEN_TITLE, placeAfter = false, layoutInfo)
        isOverHiddenArea && !hides -> source.placedNextTo(HIDDEN_TITLE, placeAfter = true, layoutInfo)
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
 * Where the group or item with [draggedKey] is dropped if the user lets go of it during [mode], if anywhere. Next to
 * the list items around it, it is dropped where it was taken from.
 *
 * @param draggedKey Key of the dragged group or item.
 * @param mode The interaction for the dragged group or item.
 * @param items The groups or items as they were before dragging.
 */
internal fun dropTargetOf(
    draggedKey: Any,
    mode: InteractionMode.List,
    items: HideableListItems,
): CustomizeMenuDropTarget? {
    val reordering = mode as? InteractionMode.List.Reordering ?: return null
    if (reordering.target.key == DROP_AREA) return CustomizeMenuDropTarget.EmptyHiddenArea
    val target = CustomizeMenuDropTarget.NextTo(reordering.target.key, reordering.placeAfter)
    return if (items.drop(draggedKey, target) == items) CustomizeMenuDropTarget.InPlace else target
}

/** Which side of a list item gets the space for the line showing where the dragged group is dropped. */
internal enum class DropLineSpace {
    ABOVE,
    BELOW,
}

/**
 * Which side of the list item with [key] gets the space for the drop line, or null if none does. When the dragged group
 * would go back where it was, it gets the space itself.
 *
 * @param key Key of the list item.
 * @param isDragged Whether the list item is the dragged group.
 * @param dropTarget Where the dragged group is dropped.
 */
internal fun dropLineSpaceOf(key: Any, isDragged: Boolean, dropTarget: CustomizeMenuDropTarget?): DropLineSpace? =
    when (dropTarget) {
        is CustomizeMenuDropTarget.NextTo ->
            when {
                dropTarget.key != key -> null
                dropTarget.placeAfter -> DropLineSpace.BELOW
                else -> DropLineSpace.ABOVE
            }
        CustomizeMenuDropTarget.InPlace -> if (isDragged) DropLineSpace.BELOW else null
        CustomizeMenuDropTarget.EmptyHiddenArea,
        null -> null
    }

/**
 * Show the drop line next to the list item with [key], if the dragged group is dropped there. The line takes space like
 * a section: the item grows by the item spacing plus the line's thickness, and records it in [extraSpace]. The spacing
 * on the other side of the line is the one every item already has above itself.
 *
 * @param key Key of the list item this is applied to.
 * @param isDragged Whether the list item is the dragged group.
 * @param dropTarget Where the dragged group is dropped, read only when measuring and drawing.
 * @param extraSpace Where the space taken by the line is recorded.
 * @param itemSpacing Space between the items of the list.
 * @param part Which part of the list item this is applied to, for an item made of several nodes.
 * @param sides Sides of the list item this takes space on for the line.
 */
@Composable
internal fun Modifier.dropLineSpace(
    key: Any,
    isDragged: Boolean,
    dropTarget: State<CustomizeMenuDropTarget?>,
    extraSpace: ExtraItemSpace,
    itemSpacing: Dp,
    part: Int = 0,
    sides: Set<DropLineSpace> = DropLineSpace.entries.toSet(),
): Modifier {
    val lineThickness = AcornTheme.layout.border.heaviest
    fun space() =
        dropLineSpaceOf(key = key, isDragged = isDragged, dropTarget = dropTarget.value)?.takeIf { it in sides }
    return this.dropLineAt(itemSpacing) { space() }
        .layout { measurable, constraints ->
            val space = space()
            val extra = if (space == null) 0 else dropLineSpacePx(itemSpacing = itemSpacing, thickness = lineThickness)
            val placeable = measurable.measure(constraints.offset(vertical = -extra))
            val height = constraints.constrainHeight(placeable.height + extra)
            extraSpace.record(key = key, extra = extra, size = height, part = part)
            layout(placeable.width, height) { placeable.place(0, if (space == DropLineSpace.ABOVE) extra else 0) }
        }
}

/** Pixels the drop line takes: the item spacing plus the line's [thickness]. */
internal fun Density.dropLineSpacePx(itemSpacing: Dp, thickness: Dp): Int =
    itemSpacing.roundToPx() + thickness.roundToPx()

/**
 * Draw the drop line on the side returned by [lineSpace], if any, in the space taken for it there.
 *
 * @param itemSpacing Space between the items of the list, left above the line when it is drawn above.
 * @param lineSpace Side to draw the line on, read only when drawing.
 */
@Composable
internal fun Modifier.dropLineAt(itemSpacing: Dp, lineSpace: () -> DropLineSpace?): Modifier {
    val color = MaterialTheme.colorScheme.tertiary
    val lineThickness = AcornTheme.layout.border.heaviest
    val horizontalInset = AcornTheme.layout.space.static200
    return this.drawBehind {
        val thickness = lineThickness.roundToPx()
        val y =
            when (lineSpace()) {
                DropLineSpace.ABOVE -> itemSpacing.roundToPx() + thickness / 2f
                DropLineSpace.BELOW -> size.height - thickness / 2f
                null -> return@drawBehind
            }
        drawLine(
            color = color,
            start = Offset(horizontalInset.toPx(), y),
            end = Offset(size.width - horizontalInset.toPx(), y),
            strokeWidth = thickness.toFloat(),
            cap = StrokeCap.Round,
        )
    }
}
