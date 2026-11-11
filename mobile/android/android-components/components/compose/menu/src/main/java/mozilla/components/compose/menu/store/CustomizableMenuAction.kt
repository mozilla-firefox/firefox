/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.lib.state.Action

/** [Action]s for updating the [CustomizableMenuState] via [CustomizableMenuStore]. */
sealed interface CustomizableMenuAction : Action {

    /** The screen has just been initialized. Dispatched automatically before any other action. */
    data object Init : CustomizableMenuAction

    /**
     * Show the current structure of the menu.
     *
     * @property items All the menu groups: the ones shown in the menu in the order they are shown in, then the ones the
     *   user removed from the menu.
     * @property mandatoryGroupIds Ids of the [items] which cannot be removed from the menu.
     * @property hiddenGroupIds Ids of the [items] removed from the menu.
     * @property isCustomized Whether this menu structure differs from the default one.
     */
    data class Update(
        val items: List<MenuItemsGroup>,
        val mandatoryGroupIds: Set<String>,
        val hiddenGroupIds: Set<String>,
        val isCustomized: Boolean,
    ) : CustomizableMenuAction

    /**
     * The user changed the structure of the menu.
     *
     * @property items All the menu groups: the ones to show in the order they should be shown in, then the ones the
     *   user removed from the menu.
     * @property hiddenGroupIds Ids of the [items] removed from the menu.
     * @property movedGroupId Id of the menu group the user moved.
     */
    data class MenuStructureChanged(
        val items: List<MenuItemsGroup>,
        val hiddenGroupIds: Set<String>,
        val movedGroupId: String,
    ) : CustomizableMenuAction

    /**
     * The menu structure started or stopped differing from the default one.
     *
     * @property isCustomized Whether the menu structure differs from the default one.
     */
    data class CustomizationStatusChanged(val isCustomized: Boolean) : CustomizableMenuAction

    /**
     * The user tried to remove a menu group which cannot be removed from the menu.
     *
     * @property sectionId Id of the menu group.
     */
    data class AttemptedRemovalOfMandatorySection(val sectionId: String) : CustomizableMenuAction

    /** The user tapped or gestured to go back while in the customization screen. */
    data object BackPressed : CustomizableMenuAction

    /** The user tapped "Reset to default" on the customize menu screen. */
    data object ResetToDefaultClicked : CustomizableMenuAction

    /** The user confirmed resetting the menu to default in the confirmation dialog. */
    data object ResetToDefaultConfirmed : CustomizableMenuAction

    /** The user tapped "Preview" on the customize menu screen. */
    data object PreviewClicked : CustomizableMenuAction
}
