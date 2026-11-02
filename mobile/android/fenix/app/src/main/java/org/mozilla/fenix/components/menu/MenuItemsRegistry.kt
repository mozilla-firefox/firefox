/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import org.mozilla.fenix.components.menu.configuration.FenixExpandableMenuItem
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.components.menu.configuration.MenuSectionConfiguration

/**
 * Registry of all [MenuItemProvider]s used by the menu.
 *
 * Everything needing the providers of a menu should get them from here to ensure working with the same.
 *
 * @property configuration The sections of the menu, in the order they should be shown in.
 * @param resolver Builds the [MenuItemProvider] for a [FenixMenuItem]. Called once for each distinct item configured,
 *   including the ones other items expand to.
 */
class MenuItemsRegistry(
    val configuration: List<MenuSectionConfiguration>,
    resolver: (FenixMenuItem) -> MenuItemProvider,
) {
    /** Map of the providers of each menu item shown, in the order the items are first configured in. */
    val providers: Map<FenixMenuItem, MenuItemProvider> =
        configuration.flatMap { it.items }.flatMap { it.withSubItems() }.distinct().associateWith(resolver)

    /** Get the [MenuItemProvider] of the given [item] if already configured to be shown in the menu. */
    operator fun get(item: FenixMenuItem): MenuItemProvider = providers.getValue(item)

    /** This item and the ones it expands to, when each of them is configured by a provider of its own. */
    private fun FenixMenuItem.withSubItems(): List<FenixMenuItem> =
        when (this) {
            is FenixExpandableMenuItem -> listOf(this) + subMenuItems
            else -> listOf(this)
        }
}
