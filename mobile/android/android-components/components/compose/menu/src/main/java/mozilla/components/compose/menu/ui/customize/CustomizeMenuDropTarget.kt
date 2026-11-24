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
 * @param layoutInfo Layout of the list showing the groups or items.
 * @param items The groups or items as they were before dragging.
 */
internal fun interactionModeOf(
    dragged: InteractionState.List.Active,
    mode: InteractionMode.List,
    layoutInfo: LazyListLayoutInfo,
    items: HideableListItems,
): InteractionMode.List {
    // Until the list lays out the dragged one taking no space, the others are not yet where they move to.
    // The dragged one must then be laid out with a size of exactly 0, as by `collapsed` in CustomizeMenu.kt,
    // or it is never dropped anywhere.
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

/**
 * Draw a line in the space between list items where a dragged group is dropped, if next to the list item with [key].
 *
 * @param key Key of the list item this is applied to.
 * @param dropTarget Where the dragged group is dropped, read only when drawing.
 * @param itemSpacing Space between the items of the list.
 * @param placeAfter Whether to draw the line only if the dragged group is dropped after the list item, or only if it is
 *   dropped before it. Null to draw it either way.
 */
@Composable
internal fun Modifier.dropLine(
    key: Any,
    dropTarget: State<CustomizeMenuDropTarget?>,
    itemSpacing: Dp,
    placeAfter: Boolean? = null,
): Modifier = dropLineAt {
    val target = dropTarget.value as? CustomizeMenuDropTarget.NextTo
    when {
        target?.key != key -> null
        placeAfter != null && target.placeAfter != placeAfter -> null
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
internal fun Modifier.dropLineAt(lineY: DrawScope.() -> Float?): Modifier {
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
