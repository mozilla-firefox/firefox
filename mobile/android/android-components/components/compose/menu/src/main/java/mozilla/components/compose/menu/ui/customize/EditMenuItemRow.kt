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
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.ui.ExpandableMenuItemHeader
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.compose.menu.ui.MenuListItem
import mozilla.components.compose.menu.ui.utils.DISABLED_ALPHA

/**
 * A row of the list of the screen customizing the items of a menu section.
 *
 * @param row What to show.
 * @param onExpansionToggle Called with the id of an expandable item when the user expands or collapses it.
 * @param modifier The [Modifier] to be applied to the row.
 */
@Composable
internal fun EditMenuItemRow(
    row: EditMenuItemsRow,
    onExpansionToggle: (itemId: String) -> Unit,
    modifier: Modifier = Modifier,
) {
    when (row) {
        is EditMenuItemsRow.Item -> ItemRow(row, onExpansionToggle, modifier)
        is EditMenuItemsRow.SubItemsEnd ->
            RowFrame(isSubItem = false, modifier = modifier) {
                SubItemsDivider()
            }
    }
}

@Composable
private fun ItemRow(
    row: EditMenuItemsRow.Item,
    onExpansionToggle: (itemId: String) -> Unit,
    modifier: Modifier,
) {
    val item = row.item.item ?: return
    val pillModifier =
        Modifier.then(if (row.isHidden) Modifier.hiddenBorder() else Modifier)
            .alpha(if (row.isHidden) DISABLED_ALPHA else 1f)
            .clip(MaterialTheme.shapes.extraLarge)

    RowFrame(
        isSubItem = row.parentId != null,
        modifier = modifier.padding(top = AcornTheme.layout.space.static50),
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
 * Lay out the content of a row with the space to its end where the drag handle goes, for the rows to line up.
 *
 * @param isSubItem Whether the row is of a sub-item, which starts further in.
 * @param modifier The [Modifier] to be applied to the row.
 * @param content The content of the row.
 */
@Composable
private fun RowFrame(isSubItem: Boolean, modifier: Modifier, content: @Composable () -> Unit) {
    Row(
        modifier =
            modifier.padding(
                start = AcornTheme.layout.space.static200 * if (isSubItem) 2 else 1,
                end = AcornTheme.layout.space.static25,
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(modifier = Modifier.weight(1f), propagateMinConstraints = true) { content() }
        Spacer(Modifier.width(AcornTheme.layout.space.static600))
    }
}

@Composable
private fun SubItemsDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(vertical = AcornTheme.layout.space.static100),
        color = MaterialTheme.colorScheme.outlineVariant,
    )
}
