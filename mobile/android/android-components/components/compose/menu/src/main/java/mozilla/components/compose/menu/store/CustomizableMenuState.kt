/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.lib.state.State

/**
 * The state of the screen allowing users to customize the menu.
 *
 * @property menuGroups All the menu groups of the screen: the ones shown in the menu in the order they are shown in,
 *   then the ones the user removed from the menu.
 * @property mandatoryGroupIds Ids of the [menuGroups] which cannot be removed from the menu.
 * @property hiddenGroupIds Ids of the [menuGroups] removed from the menu.
 * @property isCustomized Whether the menu structure differs from the default one.
 * @property editableGroupIds Ids of the [menuGroups] whose items can be edited in a screen of their own.
 */
data class CustomizableMenuState(
    val menuGroups: List<MenuItemsGroup>,
    val mandatoryGroupIds: Set<String> = emptySet(),
    val hiddenGroupIds: Set<String> = emptySet(),
    val isCustomized: Boolean = false,
    val editableGroupIds: Set<String> = emptySet(),
) : State
