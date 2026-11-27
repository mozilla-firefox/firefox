/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.unit.Dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.InteractableDragItemContainer
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.ui.ExpandableMenuItemHeader
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.compose.menu.ui.MenuListItem
import mozilla.components.compose.menu.ui.utils.DISABLED_ALPHA
import mozilla.components.compose.menu.ui.utils.DRAGGED_GROUP_ALPHA

/**
 * A row of the list of the screen customizing the items of a menu section, which the user can drag if it is of an item
 * which is not an expanded item.
 *
 * @param row What to show.
 * @param position Position in the list of the row.
 * @param dragState State of dragging the items of the screen.
 * @param itemSpacing Space above each row of the list.
 * @param actions What the rows do for accessibility services, when an expandable item is expanded or collapsed, and
 *   when the user long presses a drag handle.
 * @param isHighlighted Whether to highlight the row, as the one the open actions sheet acts on.
 * @param modifier The [Modifier] to be applied to the row.
 */
@Composable
internal fun LazyItemScope.EditMenuItemRow(
    row: EditMenuItemsRow,
    position: Int,
    dragState: CustomizeMenuDragState<CustomizeMenuItems>,
    itemSpacing: Dp,
    actions: EditMenuItemsActions,
    isHighlighted: Boolean,
    modifier: Modifier = Modifier,
) {
    val interactionState = dragState.interactionState
    val isDragged by
        remember(interactionState, row.key) { derivedStateOf { interactionState.draggedItem.key == row.key } }

    InteractableDragItemContainer(
        state = interactionState,
        key = row.key,
        position = position,
        modifier =
            modifier
                .drawnOnlyInView(row.key, dragState)
                .dragPlacement(key = row.key, isDragged = isDragged, dragState = dragState, itemSpacing = itemSpacing),
    ) {
        when (row) {
            is EditMenuItemsRow.Item ->
                ItemRow(
                    row = row,
                    actions = actions,
                    dragState = dragState,
                    dragKey = row.key.takeIf { row.isDraggable },
                    isDragged = isDragged,
                    isHighlighted = isHighlighted,
                    itemSpacing = itemSpacing,
                )
            is EditMenuItemsRow.SubItemsEnd ->
                RowFrame(isSubItem = false, dragState = dragState, dragKey = null, modifier = Modifier) {
                    SubItemsDivider()
                }
        }
    }
}

@Composable
private fun ItemRow(
    row: EditMenuItemsRow.Item,
    actions: EditMenuItemsActions,
    dragState: CustomizeMenuDragState<*>,
    dragKey: Any?,
    isDragged: Boolean,
    isHighlighted: Boolean,
    itemSpacing: Dp,
) {
    val item = row.item.item ?: return
    val pillModifier =
        Modifier.then(if (row.isHidden && !isHighlighted) Modifier.hiddenBorder() else Modifier)
            .alpha(if (row.isHidden) DISABLED_ALPHA else 1f)
            .clip(MaterialTheme.shapes.extraLarge)

    RowFrame(
        isSubItem = row.parentId != null,
        dragState = dragState,
        dragKey = dragKey,
        modifier = Modifier.alpha(if (isDragged) DRAGGED_GROUP_ALPHA else 1f).padding(top = itemSpacing),
        semantics = Modifier.editMenuItemSemantics(row, item, actions),
        isHighlighted = isHighlighted,
        isHidden = row.isHidden,
        onDragHandleLongPress = { actions.onShowActions(row.item.id) },
    ) {
        CustomizedGroupTheme(
            containerColor =
                when {
                    row.isHidden -> Color.Transparent
                    isHighlighted -> MaterialTheme.colorScheme.surfaceDim
                    else -> null
                }
        ) {
            if (item is ExpandableMenuItem) {
                ExpandableItemPill(row, item, actions.onExpansionToggle, dragState, pillModifier)
            } else {
                ItemPill(item, pillModifier)
            }
        }
    }
}

@Composable
private fun ExpandableItemPill(
    row: EditMenuItemsRow.Item,
    item: ExpandableMenuItem,
    onExpansionToggle: (itemId: String) -> Unit,
    dragState: CustomizeMenuDragState<*>,
    modifier: Modifier,
) {
    Column {
        if (row.isExpanded) SubItemsDivider()
        ExpandableMenuItemHeader(
            title = item.title,
            isExpanded = row.isExpanded,
            onClick = {
                if (dragState.interactionState.draggedItem.key != row.key) onExpansionToggle(row.item.id)
            },
            modifier = modifier,
            contentDescription = item.contentDescription,
            summary = item.summary,
            icon = item.icon,
            badge = item.badge,
            actionButtonText = item.actionButtonText,
            state = MenuItemState.DEFAULT,
        )
    }
}

@Composable
private fun ItemPill(item: MenuItem, modifier: Modifier) {
    MenuListItem(
        title = item.title,
        modifier = modifier,
        onClickEvent = null,
        onInteraction = {},
        contentDescription = item.contentDescription,
        summary = item.summary,
        icon = item.icon,
        badge = item.badge,
        state = item.state,
    )
}

/**
 * Lay out the content of a row with the handle to drag it by, or the space for it, at its end, for the rows to line up.
 *
 * @param isSubItem Whether the row is of a sub-item, which starts further in.
 * @param dragState State of dragging the items.
 * @param dragKey Key of the list item to drag, or `null` for a row which is not dragged and has only the space of the
 *   handle.
 * @param modifier The [Modifier] to be applied to the row.
 * @param semantics The [Modifier] making the row, with its content and handle, a node for accessibility services.
 * @param isHighlighted Whether to highlight the row, as the one the open actions sheet acts on.
 * @param isHidden Whether the row is of a hidden item.
 * @param onDragHandleLongPress Called when the user long presses the drag handle without moving.
 * @param content The content of the row.
 */
@Composable
private fun RowFrame(
    isSubItem: Boolean,
    dragState: CustomizeMenuDragState<*>,
    dragKey: Any?,
    modifier: Modifier,
    semantics: Modifier = Modifier,
    isHighlighted: Boolean = false,
    isHidden: Boolean = false,
    onDragHandleLongPress: () -> Unit = {},
    content: @Composable () -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    val currentOnDragHandleLongPress by rememberUpdatedState(onDragHandleLongPress)

    Row(
        modifier =
            modifier
                .padding(
                    start = AcornTheme.layout.space.static200 * if (isSubItem) 2 else 1,
                    end = AcornTheme.layout.space.static25,
                )
                .then(semantics),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier =
                Modifier.weight(1f)
                    .then(
                        dragKey?.let {
                            Modifier.dragAfterLongPress(dragState.interactionState, dragState.listState, it)
                        } ?: Modifier
                    )
                    .highlight(isHighlighted, isHidden, MaterialTheme.shapes.extraLarge),
            propagateMinConstraints = true,
        ) {
            content()
        }
        if (dragKey == null) {
            Spacer(Modifier.width(AcornTheme.layout.space.static600))
        } else {
            CustomizeMenuDragIndicator(
                modifier =
                    Modifier.dragOnVerticalMove(dragState.interactionState, dragState.listState, dragKey) {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        currentOnDragHandleLongPress()
                    }
            )
        }
    }
}

@Composable
private fun SubItemsDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(vertical = AcornTheme.layout.space.static100),
        color = MaterialTheme.colorScheme.outlineVariant,
    )
}
