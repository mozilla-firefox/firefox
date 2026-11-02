/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

/**
 * The menu structure as customized by the user: which menu sections to show and in which order.
 *
 * @property visibleSectionIds Ids of the sections to show, in the order they should be shown in.
 * @property hiddenSectionIds Ids of the sections the user removed from the menu.
 */
data class CustomizedMenuInfo(
    val visibleSectionIds: List<String>,
    val hiddenSectionIds: List<String>,
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
    val visiblePlusNew = visibleIfAvailable.toMutableList()
    default.visibleSectionIds.forEachIndexed { index, id ->
        if (id !in placedIds) {
            visiblePlusNew.add(index.coerceIn(0, visiblePlusNew.size), id)
        }
    }
    val hiddenPlusNew = hiddenIfAvailable + default.hiddenSectionIds.filter { it !in placedIds }

    return CustomizedMenuInfo(visibleSectionIds = visiblePlusNew, hiddenSectionIds = hiddenPlusNew)
}
