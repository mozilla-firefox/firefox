/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.data

/** Group of menu items to be shown in a `Menu`. */
sealed class MenuItemsGroup {
    abstract val id: String
    abstract val items: List<MenuItem>
    abstract val isSticky: Boolean

    /**
     * A group of menu items shown as a vertical list.
     *
     * @property id A unique identifier for this group.
     * @property items The items to show in this group.
     * @property isSticky Whether this group should be sticky at the top or bottom of the menu.
     */
    data class Row(
        override val id: String,
        override val items: List<MenuItem>,
        override val isSticky: Boolean = false,
    ) : MenuItemsGroup()

    /**
     * A group of menu items shown as a grid.
     *
     * @property id A unique identifier for this group.
     * @property items The items to show in this group.
     * @property isSticky Whether this group should be sticky at the top or bottom of the menu.
     */
    data class Grid(
        override val id: String,
        override val items: List<MenuItem>,
        override val isSticky: Boolean = false,
    ) : MenuItemsGroup()
}
