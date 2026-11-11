/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.MenuItemsGroup

/** Keys of the items of the customize menu screen which are not menu groups. */
internal enum class CustomizeMenuItemKey {
    DRAG_BAR,

    /** Separates the shown groups, above it, from the hidden groups, below it. */
    HIDDEN_SECTIONS,

    /** The empty area to drag groups into to hide them, shown below [HIDDEN_SECTIONS] when no other group is hidden. */
    DROP_AREA,
    RESET_BUTTON,
}

/**
 * The menu groups of a customize menu screen.
 *
 * @property shown The groups shown in the menu, in the order they are shown in.
 * @property hidden The groups the user removed from the menu.
 * @property mandatoryIds Ids of the [shown] groups which cannot be removed from the menu.
 */
internal data class CustomizeMenuGroups(
    val shown: List<MenuItemsGroup>,
    val hidden: List<MenuItemsGroup>,
    val mandatoryIds: Set<String> = emptySet(),
) {
    /** Index in the list of the hidden sections title, after the drag bar and the shown groups. */
    val hiddenSectionsIndex: Int
        get() = 1 + shown.size

    /**
     * Move a group next to another group, or to either side of [CustomizeMenuItemKey.HIDDEN_SECTIONS] to show it last
     * or hide it first. Mandatory groups cannot be hidden.
     *
     * @param sourceId Id of the group to move.
     * @param targetKey Id of the group to move it next to, or [CustomizeMenuItemKey.HIDDEN_SECTIONS].
     * @param placeAfter Whether to place the group after its target instead of before it.
     */
    fun moveGroup(sourceId: Any, targetKey: Any, placeAfter: Boolean): CustomizeMenuGroups {
        val group = (shown + hidden).firstOrNull { it.id == sourceId } ?: return this
        val hides =
            (targetKey == CustomizeMenuItemKey.HIDDEN_SECTIONS && placeAfter) || hidden.any { it.id == targetKey }
        if (targetKey == sourceId || (hides && sourceId in mandatoryIds)) return this

        val otherShown = shown.filterNot { it.id == sourceId }
        val otherHidden = hidden.filterNot { it.id == sourceId }
        return when {
            targetKey == CustomizeMenuItemKey.HIDDEN_SECTIONS && placeAfter ->
                copy(shown = otherShown, hidden = listOf(group) + otherHidden)

            targetKey == CustomizeMenuItemKey.HIDDEN_SECTIONS -> copy(shown = otherShown + group, hidden = otherHidden)

            otherShown.any { it.id == targetKey } ->
                copy(shown = otherShown.withNextTo(targetKey, group, placeAfter), hidden = otherHidden)

            otherHidden.any { it.id == targetKey } ->
                copy(shown = otherShown, hidden = otherHidden.withNextTo(targetKey, group, placeAfter))

            else -> this
        }
    }
}

private fun List<MenuItemsGroup>.withNextTo(
    targetId: Any,
    group: MenuItemsGroup,
    placeAfter: Boolean,
): List<MenuItemsGroup> {
    val index = indexOfFirst { it.id == targetId } + if (placeAfter) 1 else 0
    return toMutableList().apply { add(index, group) }
}
