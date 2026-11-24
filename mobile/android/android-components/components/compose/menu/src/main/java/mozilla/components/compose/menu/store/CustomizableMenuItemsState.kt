/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.lib.state.State

/**
 * The state of the screen allowing users to customize the items of a menu section.
 *
 * @property items The items shown in the menu, in the order they are shown in.
 * @property hiddenItems The items the user removed from the menu.
 * @property expandedItemIds Ids of the expandable items currently expanded. These are not saved.
 */
data class CustomizableMenuItemsState(
    val items: List<CustomizableMenuItem> = emptyList(),
    val hiddenItems: List<CustomizableMenuItem> = emptyList(),
    val expandedItemIds: Set<String> = emptySet(),
) : State
