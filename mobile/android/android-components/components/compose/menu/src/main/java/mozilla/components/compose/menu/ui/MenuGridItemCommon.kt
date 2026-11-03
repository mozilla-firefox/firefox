/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.CollectionItemInfo
import androidx.compose.ui.semantics.Role.Companion.Button
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.collectionItemInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.isTraversalGroup
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.Hyphens
import androidx.compose.ui.text.style.TextAlign
import mozilla.components.compose.base.badge.BADGE_SIZE_SMALL
import mozilla.components.compose.base.badge.BadgedIcon
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.information
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.store.MenuEvent

/**
 * The icon and the optional title of a menu grid item, to be shown one below the other.
 *
 * @param title The title of the menu item. Nothing is shown below the icon if it is empty.
 * @param icon The [MenuItemIcon] of the menu item.
 * @param state The [MenuItemState] of the menu item, which determines the colors.
 */
@Composable
internal fun MenuGridItemContent(
    title: Text,
    icon: MenuItemIcon,
    state: MenuItemState,
) {
    BadgedIcon(
        painter = icon.painter,
        isHighlighted = icon.isHighlighted,
        modifier = Modifier.size(AcornTheme.layout.size.static300),
        size = BADGE_SIZE_SMALL,
        contentDescription = null,
        containerColor = MaterialTheme.colorScheme.information,
        tint = state.secondaryContentColor,
    )

    if (title.value.isNotEmpty()) {
        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static50))

        Text(
            text = title.value,
            style = AcornTheme.typography.caption.copy(hyphens = Hyphens.Auto),
            color = state.contentColor,
            maxLines = 2,
            softWrap = true,
            textAlign = TextAlign.Center,
        )
    }
}

/**
 * The [Shape] of an item in a grid: fully rounded corners on the outer sides of the first and last items, and slightly
 * rounded corners where items meet.
 *
 * @param index The index of the item in the grid.
 * @param total The number of items in the grid.
 */
@Composable
internal fun getGridItemShape(
    index: Int,
    total: Int,
): Shape {
    if (total == 1) {
        return MaterialTheme.shapes.extraLarge
    }
    return when (index) {
        0 ->
            MaterialTheme.shapes.extraLarge.copy(
                topEnd = MaterialTheme.shapes.extraSmall.topEnd,
                bottomEnd = MaterialTheme.shapes.extraSmall.bottomEnd,
            )

        total - 1 ->
            MaterialTheme.shapes.extraLarge.copy(
                topStart = MaterialTheme.shapes.extraSmall.topStart,
                bottomStart = MaterialTheme.shapes.extraSmall.bottomStart,
            )

        else -> MaterialTheme.shapes.extraSmall
    }
}

/**
 * Merge the semantics of a menu grid item into a single node, as a button if it can be clicked.
 *
 * @param index The index of the item in the grid.
 * @param description The content description of the menu item.
 * @param onClickEvent The [MenuEvent] dispatched when the menu item is clicked.
 * @param onLongClickEvent The [MenuEvent] dispatched when the menu item is long-clicked.
 */
@Composable
internal fun Modifier.menuGridItemSemantics(
    index: Int?,
    description: String?,
    onClickEvent: MenuEvent?,
    onLongClickEvent: MenuEvent?,
): Modifier {
    return this.semantics(mergeDescendants = true) {
        index?.let {
            this.collectionItemInfo =
                CollectionItemInfo(
                    rowIndex = 0,
                    rowSpan = 1,
                    columnIndex = it,
                    columnSpan = 1,
                )
        }
        description?.let { this.contentDescription = it }
        if (onClickEvent != null || onLongClickEvent != null) {
            this.role = Button
        }
    }
}

/** The items of this list which are shown in a grid: banners and items without an icon are not. */
internal fun List<MenuItem>.displayedInGrid() = filter { it !is BannerMenuItem && it.icon != null }

/** Semantics of a grid of [itemCount] items laid out in a single row. */
internal fun Modifier.menuGridGroupSemantics(itemCount: Int): Modifier =
    this.semantics {
        isTraversalGroup = true
        collectionInfo = CollectionInfo(rowCount = 1, columnCount = itemCount)
    }
