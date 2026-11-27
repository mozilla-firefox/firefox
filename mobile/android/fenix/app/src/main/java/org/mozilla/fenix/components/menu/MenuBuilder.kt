/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem as ShownMenuItem
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import org.mozilla.fenix.components.menu.configuration.FenixExpandableMenuItem
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.components.menu.configuration.MenuSectionConfiguration
import org.mozilla.fenix.components.menu.configuration.asMenuItemsGroup

/**
 * Assembles a menu from the items each feature provides.
 *
 * This only knows which items may be shown, in what order and grouped how. What any one of them looks like is left to
 * its [MenuItemProvider].
 *
 * @param registry [MenuItemsRegistry] with the sections of the menu and the provider of each item in them.
 * @param configuration The sections to build, in the order they should be shown in. All their items must be in
 *   [registry].
 */
class MenuBuilder(
    private val registry: MenuItemsRegistry,
    private val configuration: List<MenuSectionConfiguration> = registry.configuration,
) {
    /** The menu to show, re-emitted whenever any of the items in it changes. */
    val menuStructure: Flow<List<MenuItemsGroup>> =
        itemsFromProviders().map { items -> configuration.toGroups(items) }.distinctUntilChanged()

    /**
     * What the feature owning each configured item currently offers for it, keyed by [FenixMenuItem.id] and re-emitted
     * whenever any one of them changes. An item that should not be shown for now is offered as an empty list, meaning
     * the provider decided that nothing should be shown for it rather than that it has not decided yet.
     */
    private fun itemsFromProviders(): Flow<Map<String, List<ShownMenuItem>>> =
        combine(registry.providers.values.map { it.itemsFlow }) { provided ->
            registry.providers.keys.zip(provided).toMap()
        }

    /**
     * A [MenuItemsGroup] for each of these sections, laid out the way the section asks for and holding only the items
     * that are currently shown. Sections left with nothing to show are dropped, and the
     * [MenuSectionConfiguration.isFirstScrollingSection] ones are then moved right below the sticky header, if any.
     */
    private fun List<MenuSectionConfiguration>.toGroups(items: Map<String, List<ShownMenuItem>>): List<MenuItemsGroup> {
        val shown = map { section ->
            section to section.asMenuItemsGroup(shownItems = section.items.flatMap { it.getMenuItemsToShow(items) })
        }
            .filterNot { (_, group) -> group.items.isEmpty() }
        val (first, rest) = shown.partition { (section, _) -> section.isFirstScrollingSection }
        val stickyHeaderCount = if (rest.firstOrNull()?.second?.isSticky == true) 1 else 0
        return (rest.take(stickyHeaderCount) + first + rest.drop(stickyHeaderCount)).map { (_, group) -> group }
    }

    /**
     * Get the menu items configuration to show for this, or an empty list if none is available.
     *
     * @param items A map of the [FenixMenuItem.id] of all the menu items wanted to be shown in the menu to the actual
     *   menu items configuration available.
     */
    private fun FenixMenuItem.getMenuItemsToShow(items: Map<String, List<ShownMenuItem>>): List<ShownMenuItem> {
        if (this !is FenixExpandableMenuItem) return items[id].orEmpty()

        val header = items[id]?.singleOrNull() as? ExpandableMenuItem ?: return emptyList()
        val children = subMenuItems.flatMap { items[it.id].orEmpty() }.filterIsInstance<StandardMenuItem>()
        if (children.isEmpty()) return emptyList()

        val provider = registry[this] as? ExpandableMenuItemProvider
        return listOfNotNull(provider?.updateWithSubMenuItems(header, children))
    }
}
