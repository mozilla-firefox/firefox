/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.MenuItemsGroup

/** Keys of the items of a customize menu screen list which are not menu groups or menu items. */
internal enum class CustomizeMenuItemKey {
    DRAG_BAR,

    /** Separates the shown groups or items, above it, from the hidden ones, below it. */
    HIDDEN_TITLE,

    /**
     * The empty area to drag groups or items into to hide them, shown below [HIDDEN_TITLE] when no other one is hidden.
     */
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
) : HideableListItems {
    override val hiddenKeys: List<Any> = hidden.map { it.id }

    /** Index in the list of the hidden sections title, after the drag bar and the shown groups. */
    override val hiddenTitleIndex: Int
        get() = 1 + shown.size

    override fun isShownKey(key: Any) = shown.any { it.id == key }

    override fun cannotBeHidden(key: Any) = key in mandatoryIds

    override fun drop(key: Any, target: CustomizeMenuDropTarget): CustomizeMenuGroups =
        when (target) {
            is CustomizeMenuDropTarget.NextTo -> moveGroup(key, target.key, target.placeAfter)
            CustomizeMenuDropTarget.EmptyHiddenArea ->
                moveGroup(key, CustomizeMenuItemKey.HIDDEN_TITLE, placeAfter = true)
            CustomizeMenuDropTarget.InPlace -> this
        }

    /**
     * Index in the list of the group with [id], or `null` if there is none. A hidden group is right below the hidden
     * sections title, as the area to drag groups into to hide them is shown only when no group is hidden.
     */
    fun listIndexOf(id: String): Int? {
        val shownIndex = shown.indexOfFirst { it.id == id }
        if (shownIndex != -1) return 1 + shownIndex
        val hiddenIndex = hidden.indexOfFirst { it.id == id }
        return if (hiddenIndex == -1) null else hiddenTitleIndex + 1 + hiddenIndex
    }

    /**
     * Move a group next to another group, or to either side of [CustomizeMenuItemKey.HIDDEN_TITLE] to show it last or
     * hide it first. Mandatory groups cannot be hidden.
     *
     * @param sourceId Id of the group to move.
     * @param targetKey Id of the group to move it next to, or [CustomizeMenuItemKey.HIDDEN_TITLE].
     * @param placeAfter Whether to place the group after its target instead of before it.
     */
    fun moveGroup(sourceId: Any, targetKey: Any, placeAfter: Boolean): CustomizeMenuGroups {
        val group = (shown + hidden).firstOrNull { it.id == sourceId } ?: return this
        val hides = (targetKey == CustomizeMenuItemKey.HIDDEN_TITLE && placeAfter) || hidden.any { it.id == targetKey }
        if (targetKey == sourceId || (hides && sourceId in mandatoryIds)) return this

        val otherShown = shown.filterNot { it.id == sourceId }
        val otherHidden = hidden.filterNot { it.id == sourceId }
        return when {
            targetKey == CustomizeMenuItemKey.HIDDEN_TITLE && placeAfter ->
                copy(shown = otherShown, hidden = listOf(group) + otherHidden)

            targetKey == CustomizeMenuItemKey.HIDDEN_TITLE -> copy(shown = otherShown + group, hidden = otherHidden)

            otherShown.any { it.id == targetKey } ->
                copy(shown = otherShown.withNextTo(targetKey, group, placeAfter), hidden = otherHidden)

            otherHidden.any { it.id == targetKey } ->
                copy(shown = otherShown, hidden = otherHidden.withNextTo(targetKey, group, placeAfter))

            else -> this
        }
    }

    /** Move the shown group with [id] above the shown group before it, or `null` if it is not shown or shown first. */
    fun moveUp(id: String): CustomizeMenuGroups? {
        val index = shown.indexOfFirst { it.id == id }
        if (index < 1) return null
        return moveGroup(id, shown[index - 1].id, placeAfter = false)
    }

    /** Move the shown group with [id] below the shown group after it, or `null` if it is not shown or shown last. */
    fun moveDown(id: String): CustomizeMenuGroups? {
        val index = shown.indexOfFirst { it.id == id }
        if (index == -1 || index == shown.lastIndex) return null
        return moveGroup(id, shown[index + 1].id, placeAfter = true)
    }

    /**
     * Hide the shown group with [id] first among the hidden groups, or `null` if it is not shown or cannot be removed
     * from the menu.
     */
    fun moveToHidden(id: String): CustomizeMenuGroups? {
        if (shown.none { it.id == id } || id in mandatoryIds) return null
        return moveGroup(id, CustomizeMenuItemKey.HIDDEN_TITLE, placeAfter = true)
    }

    /** Show the hidden group with [id] last among the shown groups, or `null` if it is not hidden. */
    fun moveToShown(id: String): CustomizeMenuGroups? {
        if (hidden.none { it.id == id }) return null
        return moveGroup(id, CustomizeMenuItemKey.HIDDEN_TITLE, placeAfter = false)
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
