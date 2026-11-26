/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.hasShownSubItems
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

        /** Whether the item can be dragged. An expanded item cannot be. */
        val isDraggable: Boolean
            get() = !isExpanded
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

/** Where in the lists of the items of a section an item is. */
internal sealed interface Container {
    /** Directly in the section, shown. */
    data object Top : Container

    /**
     * Inside the shown expandable item with [parentId].
     *
     * @property parentId Id of the expandable item.
     */
    data class SubItems(val parentId: String) : Container

    /** Directly in the section, hidden. */
    data object HiddenTop : Container

    /**
     * Inside the hidden expandable item with [parentId].
     *
     * @property parentId Id of the expandable item.
     */
    data class HiddenSubItems(val parentId: String) : Container
}

private val Container.isHidden: Boolean
    get() = this is Container.HiddenTop || this is Container.HiddenSubItems

private val Container.isTop: Boolean
    get() = this is Container.Top || this is Container.HiddenTop

private fun containerOf(parentId: String?, isHidden: Boolean): Container =
    when {
        parentId == null -> if (isHidden) Container.HiddenTop else Container.Top
        else -> if (isHidden) Container.HiddenSubItems(parentId) else Container.SubItems(parentId)
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

private val hiddenFirst = Placement(Container.HiddenTop, nextToId = null, placeAfter = false)

/** What dropping an item does to the items of a screen customizing the items of a menu section. */
internal sealed interface DropOutcome {
    /**
     * The item is moved.
     *
     * @property items The items after the move.
     */
    data class Moved(val items: CustomizeMenuItems) : DropOutcome

    /**
     * An item which can only be directly in its section is dropped inside another item, which is not allowed, and the
     * user is told.
     */
    data object RejectedAsSubItem : DropOutcome

    /** Nothing changes. */
    data object Unchanged : DropOutcome
}

/**
 * An action moving an item of a screen customizing the items of a menu section, offered to accessibility services.
 *
 * @property kind What the action does.
 * @property result The items as moved by the action.
 * @property isRefused Whether the action cannot be done, hiding the last shown item, and leaves the items unchanged.
 */
internal data class EditMenuItemAction(
    val kind: Kind,
    val result: CustomizeMenuItems,
    val isRefused: Boolean = false,
) {
    /** What an action does to its item. */
    enum class Kind {
        MOVE_UP,
        MOVE_DOWN,
        MOVE_INTO,
        MOVE_OUT_OF,
        HIDE,
        UNHIDE,
    }
}

/**
 * The items of a screen customizing the items of a menu section.
 *
 * @property shown The items shown in the menu, in the order they are shown in.
 * @property hidden The items the user removed from the menu.
 * @property expandedIds Ids of the expandable items to show expanded.
 */
@Suppress("TooManyFunctions")
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

    /** The first shown item expanding to others, the one the other items are moved into. */
    val firstShownExpandable: CustomizableMenuItem? = shown.firstOrNull { it.item is ExpandableMenuItem }

    override val hiddenKeys: List<Any> = hiddenRows.map { it.key }

    /** Index in the list of the hidden items title, after the drag bar and the rows of the shown items. */
    override val hiddenTitleIndex: Int = 1 + shownRows.size

    private val shownKeys = shownRows.mapTo(mutableSetOf()) { it.key }

    private val shownItemKeys = shownRows.filterIsInstance<EditMenuItemsRow.Item>().map { it.key }

    /** Number of the shown items and of the sub-items of expanded items, not the sub-items of collapsed items. */
    val shownWithSubItemsCount: Int = shownItemKeys.size

    /**
     * Position, from 1, of the shown item with [key] among the [shownWithSubItemsCount] shown items and sub-items, or
     * `null` if not one.
     */
    fun shownPositionOf(key: Any): Int? = shownItemKeys.indexOf(key).takeIf { it != -1 }?.plus(1)

    private val shownSubItemKeys: Map<String, List<Any>> =
        shownRows
            .filterIsInstance<EditMenuItemsRow.Item>()
            .mapNotNull { row -> row.parentId?.let { it to row.key } }
            .groupBy({ it.first }, { it.second })

    /** Number of the shown sub-items of the expanded item with [parentId], none if it is collapsed. */
    fun shownSubItemsCountOf(parentId: String): Int = shownSubItemKeys[parentId].orEmpty().size

    /**
     * Position, from 1, of the shown sub-item with [key] among the [shownSubItemsCountOf] sub-items of the expanded
     * item with [parentId], or `null` if it is not one of them.
     */
    fun shownSubItemPositionOf(parentId: String, key: Any): Int? =
        shownSubItemKeys[parentId]?.indexOf(key)?.takeIf { it != -1 }?.plus(1)

    override fun isShownKey(key: Any) = key in shownKeys

    override fun cannotBeHidden(key: Any) = isShownKey(key) && shown.without(key).none { it.isVisible }

    override fun drop(key: Any, target: CustomizeMenuDropTarget): CustomizeMenuItems =
        (dropOutcome(key, target) as? DropOutcome.Moved)?.items ?: this

    /**
     * What dropping an item next to the target does: move it next to the target, in the container of the target, hiding
     * it or showing it again if the container is a hidden one or a shown one. An expandable item, or one marked
     * [CustomizableMenuItem.isTopLevelOnly], can only be moved to the top level, hidden or shown, and an expandable
     * item keeps its sub-items. Items the app does not show keep their index in the container the item is placed in,
     * and their order in the one it leaves. Nothing changes if the target is the item itself, or if the item is the
     * last one shown on the screen and would be hidden.
     *
     * @param key Id of the item to move.
     * @param target Where to move the item to.
     */
    fun dropOutcome(key: Any, target: CustomizeMenuDropTarget): DropOutcome =
        placementOf(target)?.let { placementOutcome(key, it) } ?: DropOutcome.Unchanged

    /**
     * Move the shown item with [key] above the shown item before it in the same list, or `null` if the item is not
     * shown, cannot be dragged, or is first. Moving an expandable item keeps its sub-items, and an item never leaves
     * its list. The result is these items if the item cannot be moved there.
     */
    fun moveUp(key: Any): CustomizeMenuItems? = moveWithinList(key, offset = -1)

    /** As [moveUp], moving the item below the shown item after it in the same list, or `null` if it is last. */
    fun moveDown(key: Any): CustomizeMenuItems? = moveWithinList(key, offset = 1)

    /**
     * Move the shown item with [key] to the top of the hidden items, or `null` if the item is not shown or cannot be
     * dragged. Moving an expandable item keeps its sub-items. The result is these items if the item is the last one
     * shown on the screen.
     */
    fun moveToHidden(key: Any): CustomizeMenuItems? {
        val row = shownRows.firstOrNull { it.key == key } as? EditMenuItemsRow.Item ?: return null
        return if (row.isDraggable) moveTo(key, hiddenFirst) else null
    }

    /**
     * Move the hidden item with [key] to the shown items, after the last shown item which is not the first shown
     * expandable one, so last but for that item if it is last, or `null` if the item is not hidden or cannot be
     * dragged. The sub-item of a hidden expandable item leaves it, which stays hidden.
     */
    fun moveToShown(key: Any): CustomizeMenuItems? {
        val row = hiddenRows.firstOrNull { it.key == key } as? EditMenuItemsRow.Item ?: return null
        return if (row.isDraggable) moveTo(key, endOfTopLevel(firstShownExpandable?.id)) else null
    }

    /**
     * Move the shown item with [key] to the top of the sub-items of the shown expandable item, which stays collapsed or
     * expanded, or `null` if the item is not shown, is not at the top level, is expandable or
     * [CustomizableMenuItem.isTopLevelOnly], or there is no shown expandable item. The expandable item moved into is
     * the first shown one.
     */
    fun moveIntoExpandable(key: Any): CustomizeMenuItems? {
        val row = shownRows.firstOrNull { it.key == key } as? EditMenuItemsRow.Item ?: return null
        if (row.parentId != null || !row.item.canBeSubItem) return null
        val expandable = firstShownExpandable ?: return null
        return moveTo(key, Placement(Container.SubItems(expandable.id), nextToId = null, placeAfter = false))
    }

    /**
     * Move the shown sub-item with [key] out of the expandable item it is in, after the last shown item which is not
     * that one, so last but for that item if it is last, or `null` if the item is not a shown sub-item.
     */
    fun moveOutOfExpandable(key: Any): CustomizeMenuItems? {
        val row = shownRows.firstOrNull { it.key == key } as? EditMenuItemsRow.Item ?: return null
        val parentId = row.parentId ?: return null
        return moveTo(key, endOfTopLevel(parentId))
    }

    /**
     * The actions moving the item with [key], in the order they are offered in. None if the item has no row or cannot
     * be dragged. An action leaving the items unchanged is not offered, except hiding the last shown item, which is
     * marked [EditMenuItemAction.isRefused] and whose result is these items.
     */
    fun actionsOf(key: Any): List<EditMenuItemAction> =
        listOfNotNull(
                moveUp(key)?.let { EditMenuItemAction(EditMenuItemAction.Kind.MOVE_UP, it) },
                moveDown(key)?.let { EditMenuItemAction(EditMenuItemAction.Kind.MOVE_DOWN, it) },
                moveIntoExpandable(key)?.let { EditMenuItemAction(EditMenuItemAction.Kind.MOVE_INTO, it) },
                moveOutOfExpandable(key)?.let { EditMenuItemAction(EditMenuItemAction.Kind.MOVE_OUT_OF, it) },
                moveToHidden(key)?.let { EditMenuItemAction(EditMenuItemAction.Kind.HIDE, it) },
                moveToShown(key)?.let { EditMenuItemAction(EditMenuItemAction.Kind.UNHIDE, it) },
            )
            .mapNotNull { action ->
                when {
                    action.result != this -> action
                    action.kind == EditMenuItemAction.Kind.HIDE -> action.copy(isRefused = true)
                    else -> null
                }
            }

    /**
     * Index in the list of the row of the item with [key], or `null` if it has no row. The rows of the hidden items
     * come after the title of the hidden items.
     */
    fun listIndexOf(key: Any): Int? {
        val shownIndex = shownRows.indexOfFirst { it.key == key }
        if (shownIndex != -1) return 1 + shownIndex
        val hiddenIndex = hiddenRows.indexOfFirst { it.key == key }
        return if (hiddenIndex == -1) null else hiddenTitleIndex + 1 + hiddenIndex
    }

    private fun endOfTopLevel(expandableId: String?): Placement {
        val last = shown.lastOrNull { it.item != null && it.id != expandableId }
        return Placement(Container.Top, last?.id ?: expandableId, placeAfter = last != null)
    }

    private fun moveWithinList(key: Any, offset: Int): CustomizeMenuItems? {
        val row = shownRows.firstOrNull { it.key == key } as? EditMenuItemsRow.Item ?: return null
        if (!row.isDraggable) return null
        val container = containerOf(row.parentId, isHidden = false)
        val siblings = shownItemsIn(container)
        val neighbour = siblings.getOrNull(siblings.indexOfFirst { it.id == key } + offset) ?: return null
        return moveTo(key, Placement(container, neighbour.id, placeAfter = offset > 0))
    }

    private fun shownItemsIn(container: Container): List<CustomizableMenuItem> =
        when (container) {
            is Container.SubItems -> shown.firstOrNull { it.id == container.parentId }?.subItems.orEmpty()
            else -> shown
        }.filter { it.item != null }

    private fun moveTo(key: Any, placement: Placement): CustomizeMenuItems =
        (placementOutcome(key, placement) as? DropOutcome.Moved)?.items ?: this

    private fun placementOutcome(key: Any, placement: Placement): DropOutcome {
        val (source, container) = locate(key) ?: return DropOutcome.Unchanged
        if (placement.nextToId == key) return DropOutcome.Unchanged
        if (!placement.container.isTop && source.isTopLevelOnly) return DropOutcome.RejectedAsSubItem
        if (!placement.container.isTop && source.item is ExpandableMenuItem) return DropOutcome.Unchanged
        if (placement.container.isHidden && cannotBeHidden(key)) return DropOutcome.Unchanged

        val moved =
            if (placement.container == container) {
                updated(container) { it.reordered(key, placement) }
            } else {
                copy(shown = shown.without(key), hidden = hidden.without(key)).updated(placement.container) {
                    it.inserted(source, placement)
                }
            }
        return if (moved == null || moved == this) DropOutcome.Unchanged else DropOutcome.Moved(moved)
    }

    private fun locate(key: Any): Pair<CustomizableMenuItem, Container>? =
        shown.locate(key, Container.Top) { Container.SubItems(it) }
            ?: hidden.locate(key, Container.HiddenTop) { Container.HiddenSubItems(it) }

    private fun List<CustomizableMenuItem>.locate(
        key: Any,
        top: Container,
        subItemsOf: (parentId: String) -> Container,
    ): Pair<CustomizableMenuItem, Container>? = firstNotNullOfOrNull { parent ->
        if (parent.id == key) {
            parent to top
        } else {
            parent.subItems.find { it.id == key }?.let { it to subItemsOf(parent.id) }
        }
    }

    /**
     * These items with the list in [container] replaced by what [transform] makes of it, or `null` if [transform]
     * returns `null` or the expandable item of [container] is not there.
     */
    private fun updated(
        container: Container,
        transform: (List<CustomizableMenuItem>) -> List<CustomizableMenuItem>?,
    ): CustomizeMenuItems? =
        when (container) {
            Container.Top -> transform(shown)?.let { copy(shown = it) }
            Container.HiddenTop -> transform(hidden)?.let { copy(hidden = it) }
            is Container.SubItems -> shown.withSubItems(container.parentId, transform)?.let { copy(shown = it) }
            is Container.HiddenSubItems -> hidden.withSubItems(container.parentId, transform)?.let { copy(hidden = it) }
        }

    private fun List<CustomizableMenuItem>.withSubItems(
        parentId: String,
        transform: (List<CustomizableMenuItem>) -> List<CustomizableMenuItem>?,
    ): List<CustomizableMenuItem>? {
        val subItems = firstOrNull { it.id == parentId }?.let { transform(it.subItems) } ?: return null
        return map { if (it.id == parentId) it.copy(subItems = subItems) else it }
    }

    private fun placementOf(target: CustomizeMenuDropTarget): Placement? {
        return when (target) {
            CustomizeMenuDropTarget.InPlace -> null
            CustomizeMenuDropTarget.EmptyHiddenArea -> hiddenFirst
            is CustomizeMenuDropTarget.NextTo -> placementNextTo(target)
        }
    }

    private fun placementNextTo(target: CustomizeMenuDropTarget.NextTo): Placement? {
        if (target.key == HIDDEN_TITLE) {
            return if (target.placeAfter) hiddenFirst else Placement(Container.Top, nextToId = null, placeAfter = true)
        }

        return when (val row = (shownRows + hiddenRows).firstOrNull { it.key == target.key }) {
            is EditMenuItemsRow.Item ->
                when {
                    row.parentId != null ->
                        Placement(containerOf(row.parentId, row.isHidden), row.item.id, target.placeAfter)
                    row.isExpanded && target.placeAfter ->
                        Placement(containerOf(row.item.id, row.isHidden), nextToId = null, placeAfter = false)
                    else -> Placement(containerOf(null, row.isHidden), row.item.id, target.placeAfter)
                }
            is EditMenuItemsRow.SubItemsEnd ->
                if (target.placeAfter) {
                    Placement(containerOf(null, row.isHidden), row.parentId, placeAfter = true)
                } else {
                    Placement(containerOf(row.parentId, row.isHidden), nextToId = null, placeAfter = true)
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

private val CustomizableMenuItem.canBeSubItem: Boolean
    get() = item !is ExpandableMenuItem && !isTopLevelOnly

private val CustomizableMenuItem.isVisible: Boolean
    get() = if (item is ExpandableMenuItem) hasShownSubItems else item != null
