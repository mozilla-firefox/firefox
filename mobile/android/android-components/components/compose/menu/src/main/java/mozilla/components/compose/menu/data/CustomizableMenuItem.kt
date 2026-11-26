/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.data

/**
 * A menu item of a section whose items the user can customize.
 *
 * @property id Stable id of the item, used to save where it is placed.
 * @property item How the item looks, or `null` while the app does not show it, in which case it keeps its place but is
 *   not shown on the customize screen.
 * @property subItems The items this one expands to, if it is an [ExpandableMenuItem].
 * @property isTopLevelOnly Whether the item can only be directly in its section, never inside another item.
 */
data class CustomizableMenuItem(
    val id: String,
    val item: MenuItem?,
    val subItems: List<CustomizableMenuItem> = emptyList(),
    val isTopLevelOnly: Boolean = false,
)

/** Whether this item has a sub-item the app shows. */
internal val CustomizableMenuItem.hasShownSubItems: Boolean
    get() = subItems.any { it.item != null }
