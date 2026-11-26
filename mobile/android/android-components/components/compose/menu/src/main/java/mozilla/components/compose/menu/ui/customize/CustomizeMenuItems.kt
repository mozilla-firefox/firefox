/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE

/**
 * A row of the list of a screen customizing the items of a menu section.
 *
 * @property key Key of the row in the list. It is a [String] to be saved in a bundle, so item ids cannot contain `#`.
 */
internal sealed interface EditMenuItemsRow {
    val key: String

    /**
     * A row showing a menu item.
     *
     * @property item The item.
     * @property parentId Id of the expandable item this is shown under, or `null` if it is not a sub-item.
     * @property isHidden Whether the user removed the item from the menu.
     * @property isExpanded Whether the item is expandable and expanded.
     */
    data class Item(
        val item: CustomizableMenuItem,
        val parentId: String?,
        val isHidden: Boolean,
        val isExpanded: Boolean,
    ) : EditMenuItemsRow {
        override val key: String = item.id
    }

    /**
     * The end of the sub-items of an expanded item, where items can also be dropped into it when it has none.
     *
     * @property parentId Id of the expanded item.
     * @property isHidden Whether the user removed the expanded item from the menu.
     */
    data class SubItemsEnd(val parentId: String, val isHidden: Boolean) : EditMenuItemsRow {
        override val key: String = subItemsEndKey(parentId)
    }
}

/** Key of the [EditMenuItemsRow.SubItemsEnd] of the item with [parentId]. */
internal fun subItemsEndKey(parentId: String) = "$parentId#sub_items_end"

/** Where in the list of the shown items of a section an item is. */
internal sealed interface Container {
    /** Directly in the section. */
    data object Top : Container

    /**
     * Inside the expandable item with [parentId].
     *
     * @property parentId Id of the expandable item.
     */
    data class SubItems(val parentId: String) : Container
}

/**
 * Where to place a moved item: next to the item with [nextToId], or first or last of the container if that is `null`.
 *
 * @property container The container to place the item in.
 * @property nextToId Id of the item to place it next to, if any.
 * @property placeAfter Whether to place the item after the item with [nextToId], or last if there is none, instead of
 *   before it, or first.
 */
private data class Placement(val container: Container, val nextToId: String?, val placeAfter: Boolean)

/**
 * The items of a screen customizing the items of a menu section.
 *
 * @property shown The items shown in the menu, in the order they are shown in.
 * @property hidden The items the user removed from the menu.
 * @property expandedIds Ids of the expandable items to show expanded.
 */
internal data class CustomizeMenuItems(
    val shown: List<CustomizableMenuItem>,
    val hidden: List<CustomizableMenuItem>,
    val expandedIds: Set<String>,
) : HideableListItems {
    /**
     * The rows of the shown items, in the order they are listed in. Items the app does not show are skipped, as are the
     * sub-items of collapsed items.
     */
    val shownRows: List<EditMenuItemsRow> = shown.flatMap { it.rows(isHidden = false) }

    /**
     * The rows of the hidden items, in the order they are listed in. Items the app does not show are skipped, as are
     * the sub-items of collapsed items.
     */
    val hiddenRows: List<EditMenuItemsRow> = hidden.flatMap { it.rows(isHidden = true) }

    override val hiddenKeys: List<Any> = hiddenRows.map { it.key }

    /** Index in the list of the hidden items title, after the drag bar and the rows of the shown items. */
    override val hiddenTitleIndex: Int = 1 + shownRows.size

    private val shownKeys = shownRows.mapTo(mutableSetOf()) { it.key }

    override fun isShownKey(key: Any) = key in shownKeys

    override fun cannotBeHidden(key: Any) = true

    override fun drop(key: Any, target: CustomizeMenuDropTarget): CustomizeMenuItems = move(key, target)

    /**
     * Move a shown item next to the target, in the container of the target. An expandable item can only be moved to the
     * top level. Items the app does not show keep their index in the container the item is placed in, and their order
     * in the one it leaves. Nothing changes if the target is on a hidden item or is the item itself.
     *
     * @param key Id of the item to move.
     * @param target Where to move the item to.
     */
    fun move(key: Any, target: CustomizeMenuDropTarget): CustomizeMenuItems {
        if (target !is CustomizeMenuDropTarget.NextTo) return this
        val (source, container) = locate(key) ?: return this
        val placement = placementOf(target) ?: return this
        if (placement.nextToId == key) return this
        if (placement.container != Container.Top && source.item is ExpandableMenuItem) return this

        if (placement.container != container) {
            return shown.without(key).placed(source, placement)?.let { copy(shown = it) } ?: this
        }

        return when (container) {
            Container.Top -> copy(shown = shown.reordered(key, placement))
            is Container.SubItems ->
                copy(shown = shown.map { it.withReorderedSubItems(container.parentId, key, placement) })
        }
    }

    private fun CustomizableMenuItem.withReorderedSubItems(parentId: String, sourceId: Any, placement: Placement) =
        if (id == parentId) copy(subItems = subItems.reordered(sourceId, placement)) else this

    private fun locate(key: Any): Pair<CustomizableMenuItem, Container>? = shown.firstNotNullOfOrNull { parent ->
        if (parent.id == key) {
            parent to Container.Top
        } else {
            parent.subItems.find { it.id == key }?.let { it to Container.SubItems(parent.id) }
        }
    }

    private fun placementOf(target: CustomizeMenuDropTarget.NextTo): Placement? {
        if (target.key == HIDDEN_TITLE) {
            return if (target.placeAfter) null else Placement(Container.Top, nextToId = null, placeAfter = true)
        }

        return when (val row = shownRows.firstOrNull { it.key == target.key }) {
            is EditMenuItemsRow.Item ->
                when {
                    row.parentId != null -> Placement(Container.SubItems(row.parentId), row.item.id, target.placeAfter)
                    row.isExpanded && target.placeAfter ->
                        Placement(Container.SubItems(row.item.id), nextToId = null, placeAfter = false)
                    else -> Placement(Container.Top, row.item.id, target.placeAfter)
                }
            is EditMenuItemsRow.SubItemsEnd ->
                if (target.placeAfter) {
                    Placement(Container.Top, row.parentId, placeAfter = true)
                } else {
                    Placement(Container.SubItems(row.parentId), nextToId = null, placeAfter = true)
                }
            null -> null
        }
    }

    private fun CustomizableMenuItem.rows(isHidden: Boolean): List<EditMenuItemsRow> {
        if (item == null) return emptyList()
        if (item !is ExpandableMenuItem) {
            return listOf(EditMenuItemsRow.Item(this, parentId = null, isHidden = isHidden, isExpanded = false))
        }

        val isExpanded = id in expandedIds
        val header = EditMenuItemsRow.Item(this, parentId = null, isHidden = isHidden, isExpanded = isExpanded)
        if (!isExpanded) return listOf(header)

        val subItems =
            subItems
                .filter { it.item != null }
                .map { EditMenuItemsRow.Item(it, parentId = id, isHidden = isHidden, isExpanded = false) }
        return listOf(header) + subItems + EditMenuItemsRow.SubItemsEnd(parentId = id, isHidden = isHidden)
    }
}

private fun List<CustomizableMenuItem>.reordered(sourceId: Any, placement: Placement): List<CustomizableMenuItem> {
    val visible = filter { it.item != null }
    val source = visible.firstOrNull { it.id == sourceId } ?: return this
    return inserted(source, placement, visible.filter { it.id != sourceId }) ?: this
}

private fun List<CustomizableMenuItem>.without(id: Any): List<CustomizableMenuItem> = filter {
    it.id != id
}
    .map { it.copy(subItems = it.subItems.filter { subItem -> subItem.id != id }) }

/** This with [source] placed as [placement] says, or `null` if the item it goes next to is not there. */
private fun List<CustomizableMenuItem>.placed(
    source: CustomizableMenuItem,
    placement: Placement,
): List<CustomizableMenuItem>? =
    when (val container = placement.container) {
        Container.Top -> inserted(source, placement)
        is Container.SubItems -> {
            val subItems = firstOrNull { it.id == container.parentId }?.subItems?.inserted(source, placement)
            subItems?.let { new -> map { if (it.id == container.parentId) it.copy(subItems = new) else it } }
        }
    }

/**
 * This with [source] inserted at [placement] among the [others], the items of this the app shows without [source].
 * Items the app does not show keep their index, so the item that makes the list longer ends up after trailing ones.
 * Returns `null` if the item [placement] is next to is not in [others].
 */
private fun List<CustomizableMenuItem>.inserted(
    source: CustomizableMenuItem,
    placement: Placement,
    others: List<CustomizableMenuItem> = filter { it.item != null },
): List<CustomizableMenuItem>? {
    val index = others.indexFor(placement) ?: return null
    val reordered = (others.take(index) + source + others.drop(index)).iterator()
    return map { if (it.item == null) it else reordered.next() } + reordered.asSequence()
}

private fun List<CustomizableMenuItem>.indexFor(placement: Placement): Int? =
    when (placement.nextToId) {
        null -> if (placement.placeAfter) size else 0
        else ->
            indexOfFirst { it.id == placement.nextToId }
                .takeIf { it != -1 }
                ?.let { if (placement.placeAfter) it + 1 else it }
    }
