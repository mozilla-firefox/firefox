/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem

/**
 * A row of the list of a screen customizing the items of a menu section.
 *
 * @property key Key of the row in the list. It is a [String] to be saved in a bundle, so item ids cannot contain `#`.
 */
internal sealed interface EditMenuItemsRow {
    val key: String

    /**
     * A row showing a menu item.
     *
     * @property item The item.
     * @property parentId Id of the expandable item this is shown under, or `null` if it is not a sub-item.
     * @property isHidden Whether the user removed the item from the menu.
     * @property isExpanded Whether the item is expandable and expanded.
     */
    data class Item(
        val item: CustomizableMenuItem,
        val parentId: String?,
        val isHidden: Boolean,
        val isExpanded: Boolean,
    ) : EditMenuItemsRow {
        override val key: String = item.id
    }

    /**
     * The end of the sub-items of an expanded item, where items can also be dropped into it when it has none.
     *
     * @property parentId Id of the expanded item.
     * @property isHidden Whether the user removed the expanded item from the menu.
     */
    data class SubItemsEnd(val parentId: String, val isHidden: Boolean) : EditMenuItemsRow {
        override val key: String = subItemsEndKey(parentId)
    }
}

/** Key of the [EditMenuItemsRow.SubItemsEnd] of the item with [parentId]. */
internal fun subItemsEndKey(parentId: String) = "$parentId#sub_items_end"

/**
 * The items of a screen customizing the items of a menu section.
 *
 * @property shown The items shown in the menu, in the order they are shown in.
 * @property hidden The items the user removed from the menu.
 * @property expandedIds Ids of the expandable items to show expanded.
 */
internal data class CustomizeMenuItems(
    val shown: List<CustomizableMenuItem>,
    val hidden: List<CustomizableMenuItem>,
    val expandedIds: Set<String>,
) {
    /**
     * The rows of the shown items, in the order they are listed in. Items the app does not show are skipped, as are the
     * sub-items of collapsed items.
     */
    val shownRows: List<EditMenuItemsRow> = shown.flatMap { it.rows(isHidden = false) }

    /**
     * The rows of the hidden items, in the order they are listed in. Items the app does not show are skipped, as are
     * the sub-items of collapsed items.
     */
    val hiddenRows: List<EditMenuItemsRow> = hidden.flatMap { it.rows(isHidden = true) }

    private fun CustomizableMenuItem.rows(isHidden: Boolean): List<EditMenuItemsRow> {
        if (item == null) return emptyList()
        if (item !is ExpandableMenuItem) {
            return listOf(EditMenuItemsRow.Item(this, parentId = null, isHidden = isHidden, isExpanded = false))
        }

        val isExpanded = id in expandedIds
        val header = EditMenuItemsRow.Item(this, parentId = null, isHidden = isHidden, isExpanded = isExpanded)
        if (!isExpanded) return listOf(header)

        val subItems =
            subItems
                .filter { it.item != null }
                .map { EditMenuItemsRow.Item(it, parentId = id, isHidden = isHidden, isExpanded = false) }
        return listOf(header) + subItems + EditMenuItemsRow.SubItemsEnd(parentId = id, isHidden = isHidden)
    }
}
