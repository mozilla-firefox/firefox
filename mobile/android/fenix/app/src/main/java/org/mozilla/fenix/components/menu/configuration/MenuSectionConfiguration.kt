/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemsGroup

/**
 * Configuration of a menu section.
 *
 * @property id A unique identifier for this section.
 * @property presentationMode How the items in this section should be shown.
 * @property items The items to show in this section.
 * @property isSticky Whether this section should be sticky at the top or bottom of the menu.
 */
data class MenuSectionConfiguration(
    val id: String,
    val presentationMode: MenuPresentationMode,
    val items: List<FenixMenuItem>,
    val isSticky: Boolean = false,
)

/**
 * Map this [MenuSectionConfiguration] to a [MenuItemsGroup].
 *
 * @param shownItems The items to show in this menu group.
 */
internal fun MenuSectionConfiguration.asMenuItemsGroup(shownItems: List<MenuItem>) =
    when (presentationMode) {
        MenuPresentationMode.Row ->
            MenuItemsGroup.Row(
                id = id,
                items = shownItems,
                isSticky = isSticky,
            )
        MenuPresentationMode.Grid ->
            MenuItemsGroup.Grid(
                id = id,
                items = shownItems,
                isSticky = isSticky,
            )
    }

/** How the [FenixMenuItem] should be shown inside a menu section. */
sealed class MenuPresentationMode {

    /** Show the items in a row. */
    data object Row : MenuPresentationMode()

    /** Show the items in a grid. */
    data object Grid : MenuPresentationMode()
}
