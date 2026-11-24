/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

/**
 * The menu structure as customized by the user: which menu sections to show and in which order.
 *
 * @property visibleSectionIds Ids of the sections to show, in the order they should be shown in.
 * @property hiddenSectionIds Ids of the sections the user removed from the menu.
 * @property isNavigationSectionMoved Whether the user moved the navigation section, which is then shown where it was
 *   saved until the menu is restored to its default structure. Otherwise it is shown on the same side of the menu as
 *   the toolbar.
 */
data class CustomizedMenuInfo(
    val visibleSectionIds: List<String>,
    val hiddenSectionIds: List<String>,
    val isNavigationSectionMoved: Boolean = false,
)

/**
 * The items of a menu section as customized by the user: which to show, in which order and which item expands to which
 * other items.
 *
 * @property sectionId Id of the section showing these items.
 * @property items The items to show, in the order they should be shown in.
 * @property hiddenItems The items the user removed from the section.
 */
data class CustomizedSectionInfo(
    val sectionId: String,
    val items: List<CustomizedItemInfo>,
    val hiddenItems: List<CustomizedItemInfo>,
)

/**
 * A menu item as customized by the user.
 *
 * @property id [org.mozilla.fenix.components.menu.configuration.FenixMenuItem.id] of the item.
 * @property subItemIds Ids of the items this expands to, in the order they should be shown in.
 */
data class CustomizedItemInfo(
    val id: String,
    val subItemIds: List<String> = emptyList(),
)

/**
 * Adapt this to the sections currently available in the app - the ones from [default].
 *
 * This also handles the migration of items removed or added to the application:
 * - keep only the items that are available in the application and
 * - add new items which don't yet appear in the menu customization info, shown or hidden as in [default].
 *
 * @param default The structure of the menu for users who didn't customize it.
 * @param requiredSectionIds Ids of the sections which cannot be removed from the menu.
 */
internal fun CustomizedMenuInfo.reconciledWith(
    default: CustomizedMenuInfo,
    requiredSectionIds: Set<String>,
): CustomizedMenuInfo {
    val available = (default.visibleSectionIds + default.hiddenSectionIds).toSet()
    val visibleIfAvailable = visibleSectionIds.filter { it in available }.distinct()
    val hiddenIfAvailable =
        hiddenSectionIds.filter { it in available && it !in visibleIfAvailable && it !in requiredSectionIds }.distinct()
    val placedIds = (visibleIfAvailable + hiddenIfAvailable).toSet()

    // Add new items available in the application but not present in the customized menu configuration.
    val visiblePlusNew = visibleIfAvailable.plusMissingAtDefaultIndex(default.visibleSectionIds, placedIds)
    val hiddenPlusNew = hiddenIfAvailable + default.hiddenSectionIds.filter { it !in placedIds }

    return copy(visibleSectionIds = visiblePlusNew, hiddenSectionIds = hiddenPlusNew)
}

/**
 * Adapt this to the items currently available in the section - the ones from [default].
 *
 * Same as for the menu sections, items removed from the application are dropped and new ones are added, shown or hidden
 * as in [default] and inside the same item they are expanded to from in [default]. Each item is kept only once and only
 * the items expanding to others in [default] keep the items they expand to, even if left with none of them. If nothing
 * is left to show in the menu, which an item expanding to no others does not, the section is restored to [default].
 *
 * @param default The items of the section for users who didn't customize them.
 */
internal fun CustomizedSectionInfo.reconciledWith(default: CustomizedSectionInfo): CustomizedSectionInfo {
    val defaultItems = (default.items + default.hiddenItems).associateBy { it.id }
    val expandableIds = defaultItems.values.filter { it.subItemIds.isNotEmpty() }.map { it.id }.toSet()
    val available = defaultItems.keys + defaultItems.values.flatMap { it.subItemIds }
    val placedIds = mutableSetOf<String>()

    fun List<CustomizedItemInfo>.availableOnly() = mapNotNull { item ->
        if (item.id !in available || !placedIds.add(item.id)) return@mapNotNull null
        val subItemIds =
            when (item.id in expandableIds) {
                true -> item.subItemIds.filter { it in available && it !in expandableIds && placedIds.add(it) }
                false -> emptyList()
            }
        item.copy(subItemIds = subItemIds)
    }
    val shownIfAvailable = items.availableOnly()
    val hiddenIfAvailable = hiddenItems.availableOnly()

    val shownPlusNew = shownIfAvailable.map { it.id }.plusMissingAtDefaultIndex(default.items.map { it.id }, placedIds)
    val hiddenPlusNew = hiddenIfAvailable.map { it.id } + default.hiddenItems.map { it.id }.filter { it !in placedIds }

    val subItemIds = (shownIfAvailable + hiddenIfAvailable).associate { it.id to it.subItemIds }
    fun String.withSubItemsPlusNew() =
        CustomizedItemInfo(
            id = this,
            subItemIds =
                subItemIds[this]
                    .orEmpty()
                    .plusMissingAtDefaultIndex(defaultItems[this]?.subItemIds.orEmpty(), placedIds),
        )

    val shown = shownPlusNew.map { it.withSubItemsPlusNew() }
    if (shown.all { it.id in expandableIds && it.subItemIds.isEmpty() }) return default

    return copy(items = shown, hiddenItems = hiddenPlusNew.map { it.withSubItemsPlusNew() })
}

/** These ids plus the ones from [default] not in [placedIds], each added at its index in [default] if possible. */
private fun List<String>.plusMissingAtDefaultIndex(default: List<String>, placedIds: Set<String>): List<String> {
    val result = toMutableList()
    default.forEachIndexed { index, id ->
        if (id !in placedIds) {
            result.add(index.coerceIn(0, result.size), id)
        }
    }
    return result
}
