/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import androidx.annotation.StringRes
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemsGroup

/**
 * Configuration of a menu section.
 *
 * @property id A unique identifier for this section.
 * @property presentationMode How the items in this section should be shown.
 * @property items The items to show in this section.
 * @property isSticky Whether this section should be sticky at the top or bottom of the menu.
 * @property name Name of this section, read by accessibility services before the names of its items.
 * @property showTitles Whether the titles of the items are shown when the section is a [MenuPresentationMode.Grid].
 * @property isFirstScrollingSection Whether this section is shown before the other sections that scroll, right below
 *   the sticky header if the first shown section is one.
 */
data class MenuSectionConfiguration(
    val id: String,
    val presentationMode: MenuPresentationMode,
    val items: List<FenixMenuItem>,
    val isSticky: Boolean = false,
    @param:StringRes val name: Int? = null,
    val showTitles: Boolean = true,
    val isFirstScrollingSection: Boolean = false,
)

/**
 * Map this [MenuSectionConfiguration] to a [MenuItemsGroup].
 *
 * @param shownItems The items to show in this menu group.
 */
internal fun MenuSectionConfiguration.asMenuItemsGroup(shownItems: List<MenuItem>): MenuItemsGroup {
    val groupName = name?.let { Text.Resource(it) }
    return when (presentationMode) {
        MenuPresentationMode.Row ->
            MenuItemsGroup.Row(
                id = id,
                items = shownItems,
                isSticky = isSticky,
                name = groupName,
            )
        MenuPresentationMode.Grid ->
            MenuItemsGroup.Grid(
                id = id,
                items = shownItems,
                isSticky = isSticky,
                name = groupName,
                showTitles = showTitles,
            )
    }
}

/** How the [FenixMenuItem] should be shown inside a menu section. */
sealed class MenuPresentationMode {

    /** Show the items in a row. */
    data object Row : MenuPresentationMode()

    /** Show the items in a grid. */
    data object Grid : MenuPresentationMode()
}
