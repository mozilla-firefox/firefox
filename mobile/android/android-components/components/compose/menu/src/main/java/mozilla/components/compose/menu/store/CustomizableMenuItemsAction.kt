/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.lib.state.Action

/** [Action]s for updating the [CustomizableMenuItemsState] via [CustomizableMenuItemsStore]. */
sealed interface CustomizableMenuItemsAction : Action {

    /** The screen has just been initialized. Dispatched automatically before any other action. */
    data object Init : CustomizableMenuItemsAction

    /**
     * Show the current items of the section. The shown expandable items start expanded the first time they are shown,
     * the hidden ones start collapsed.
     *
     * @property items The items shown in the menu, in the order they are shown in.
     * @property hiddenItems The items the user removed from the menu.
     */
    data class Update(
        val items: List<CustomizableMenuItem>,
        val hiddenItems: List<CustomizableMenuItem>,
    ) : CustomizableMenuItemsAction

    /**
     * The user moved an item.
     *
     * @property items The items shown in the menu, in the order they are shown in, as moved.
     * @property hiddenItems The items the user removed from the menu, as moved.
     */
    data class ItemsChanged(
        val items: List<CustomizableMenuItem>,
        val hiddenItems: List<CustomizableMenuItem>,
    ) : CustomizableMenuItemsAction

    /**
     * The user expanded or collapsed an expandable item.
     *
     * @property itemId Id of the item.
     */
    data class ExpansionToggled(val itemId: String) : CustomizableMenuItemsAction

    /** The user tapped "Preview" on the screen. */
    data object PreviewClicked : CustomizableMenuItemsAction
}
