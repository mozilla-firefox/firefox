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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
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
 * A row of the list of the screen customizing the items of a menu section, which the user can drag if it is of a shown
 * item which is not expandable.
 *
 * @param row What to show.
 * @param position Position in the list of the row.
 * @param dragState State of dragging the items of the screen.
 * @param itemSpacing Space above each row of the list.
 * @param onExpansionToggle Called with the id of an expandable item when the user expands or collapses it.
 * @param modifier The [Modifier] to be applied to the row.
 */
@Composable
internal fun LazyItemScope.EditMenuItemRow(
    row: EditMenuItemsRow,
    position: Int,
    dragState: CustomizeMenuDragState<CustomizeMenuItems>,
    itemSpacing: Dp,
    onExpansionToggle: (itemId: String) -> Unit,
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
                ItemRow(row, onExpansionToggle, dragState, row.key.takeIf { row.isDraggable }, isDragged, itemSpacing)
            is EditMenuItemsRow.SubItemsEnd ->
                RowFrame(isSubItem = false, dragState = dragState, dragKey = null, modifier = Modifier) {
                    SubItemsDivider()
                }
        }
    }
}

private val EditMenuItemsRow.Item.isDraggable: Boolean
    get() = !isHidden && item.item !is ExpandableMenuItem

@Composable
private fun ItemRow(
    row: EditMenuItemsRow.Item,
    onExpansionToggle: (itemId: String) -> Unit,
    dragState: CustomizeMenuDragState<*>,
    dragKey: Any?,
    isDragged: Boolean,
    itemSpacing: Dp,
) {
    val item = row.item.item ?: return
    val pillModifier =
        Modifier.then(if (row.isHidden) Modifier.hiddenBorder() else Modifier)
            .alpha(if (row.isHidden) DISABLED_ALPHA else 1f)
            .clip(MaterialTheme.shapes.extraLarge)

    RowFrame(
        isSubItem = row.parentId != null,
        dragState = dragState,
        dragKey = dragKey,
        modifier = Modifier.alpha(if (isDragged) DRAGGED_GROUP_ALPHA else 1f).padding(top = itemSpacing),
    ) {
        CustomizedGroupTheme(containerColor = if (row.isHidden) Color.Transparent else null) {
            if (item is ExpandableMenuItem) {
                ExpandableItemPill(row, item, onExpansionToggle, pillModifier)
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
    modifier: Modifier,
) {
    Column {
        if (row.isExpanded) SubItemsDivider()
        ExpandableMenuItemHeader(
            title = item.title,
            isExpanded = row.isExpanded,
            onClick = { onExpansionToggle(row.item.id) },
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
 * @param content The content of the row.
 */
@Composable
private fun RowFrame(
    isSubItem: Boolean,
    dragState: CustomizeMenuDragState<*>,
    dragKey: Any?,
    modifier: Modifier,
    content: @Composable () -> Unit,
) {
    Row(
        modifier =
            modifier.padding(
                start = AcornTheme.layout.space.static200 * if (isSubItem) 2 else 1,
                end = AcornTheme.layout.space.static25,
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier =
                Modifier.weight(1f)
                    .then(
                        dragKey?.let {
                            Modifier.dragAfterLongPress(dragState.interactionState, dragState.listState, it)
                        } ?: Modifier
                    ),
            propagateMinConstraints = true,
        ) {
            content()
        }
        if (dragKey == null) {
            Spacer(Modifier.width(AcornTheme.layout.space.static600))
        } else {
            CustomizeMenuDragIndicator(
                modifier =
                    Modifier.dragOnVerticalMove(
                        dragState.interactionState,
                        dragState.listState,
                        dragKey,
                        onLongPress = null,
                    )
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
