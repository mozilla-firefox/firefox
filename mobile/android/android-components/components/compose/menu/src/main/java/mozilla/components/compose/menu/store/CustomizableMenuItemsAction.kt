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
     * Show the current items of the section. The shown expandable items with items inside start expanded the first time
     * they are shown, the others and the hidden ones start collapsed.
     *
     * @property items The items shown in the menu, in the order they are shown in.
     * @property hiddenItems The items the user removed from the menu.
     */
    data class Update(
        val items: List<CustomizableMenuItem>,
        val hiddenItems: List<CustomizableMenuItem>,
    ) : CustomizableMenuItemsAction

    /**
     * The user moved an item. An expandable item the move leaves with no items inside is collapsed.
     *
     * @property items The items shown in the menu, in the order they are shown in, as moved.
     * @property hiddenItems The items the user removed from the menu, as moved.
     * @property movedItemId Id of the item the user moved.
     */
    data class ItemsChanged(
        val items: List<CustomizableMenuItem>,
        val hiddenItems: List<CustomizableMenuItem>,
        val movedItemId: String,
    ) : CustomizableMenuItemsAction

    /**
     * The user expanded or collapsed an expandable item.
     *
     * @property itemId Id of the item.
     */
    data class ExpansionToggled(val itemId: String) : CustomizableMenuItemsAction

    /**
     * The user let go of an item inside another one, which it cannot be inside of.
     *
     * @property itemId Id of the item.
     */
    data class AttemptedMoveAsSubItem(val itemId: String) : CustomizableMenuItemsAction

    /**
     * The user tried to remove the last item shown in the section from the menu, which would leave the section empty.
     *
     * @property itemId Id of the item.
     */
    data class AttemptedEmptySection(val itemId: String) : CustomizableMenuItemsAction

    /** The user tapped "Preview" on the screen. */
    data object PreviewClicked : CustomizableMenuItemsAction

    /** The user tapped or gestured to go back while in the screen customizing the items of a menu section. */
    data object BackPressed : CustomizableMenuItemsAction
}
