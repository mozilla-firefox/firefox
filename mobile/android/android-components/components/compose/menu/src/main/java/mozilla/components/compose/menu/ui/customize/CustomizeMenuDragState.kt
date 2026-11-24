/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import kotlinx.coroutines.flow.first
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.draggable.ListInteractionState
import mozilla.components.compose.draggable.createListInteractionState
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON
import mozilla.components.compose.menu.ui.utils.ext.saveMove

/**
 * State of dragging the items of a customize menu screen list.
 *
 * @property items The shown and the hidden items.
 * @property listState State of the list showing the items.
 * @property interactionState State of dragging the items of the list.
 * @property isDragging Whether an item is dragged.
 * @property hasHiddenItems Whether items other than the dragged one are hidden. Otherwise the area to drag items into
 *   to hide them is shown.
 * @property dropTarget Where the dragged item is moved to if the user lets go of it, if anywhere.
 * @property extraSpace Space the list items take, or give up, only to show the drop line, left out when picking where
 *   the dragged item is dropped.
 * @property draggedShownItem Where the dragged item was when the drag started, if it was among the shown items.
 */
internal class CustomizeMenuDragState<T : HideableListItems>(
    val items: T,
    val listState: LazyListState,
    val interactionState: ListInteractionState,
    val isDragging: Boolean,
    val hasHiddenItems: Boolean,
    val dropTarget: State<CustomizeMenuDropTarget?>,
    val extraSpace: ExtraItemSpace,
    val draggedShownItem: State<DraggedShownItem?>,
) {
    /**
     * Index in the list of the first hidden item, after the drag bar, the shown items, the hidden items title and the
     * area to drag items into if shown.
     */
    val firstHiddenIndex = items.hiddenTitleIndex + if (hasHiddenItems) 1 else 2
}

/**
 * Where the dragged item was in the list when the drag started, if it was among the shown items.
 *
 * @property index Index of the item in the list.
 * @property size Size of the item in the list.
 */
internal data class DraggedShownItem(val index: Int, val size: Int)

// Groups or items can be dropped next to these list items.
internal val FIXED_ITEMS = setOf(HIDDEN_TITLE, DROP_AREA)

private const val AUTO_SCROLL_ZONE_WINDOW_FRACTION = 0.1f

/**
 * Allows dragging the groups of the customize menu screen shown in the list with [listState] to reorder them, hide them
 * or show them again, saving the move to [store] when the user lets go of a dragged group. Tells [store], once for each
 * drag, when a group which cannot be removed from the menu is dragged over the area holding the hidden groups. The user
 * feels it whenever the line, or the highlighted area, showing where the dragged group is dropped appears or moves to
 * another place.
 */
@Composable
internal fun rememberCustomizeMenuDragState(
    store: CustomizableMenuStore,
    menuState: CustomizableMenuState,
    listState: LazyListState,
): CustomizeMenuDragState<CustomizeMenuGroups> {
    val groups =
        remember(menuState.menuGroups, menuState.mandatoryGroupIds, menuState.hiddenGroupIds) {
            val (hidden, shown) = menuState.menuGroups.partition { it.id in menuState.hiddenGroupIds }
            CustomizeMenuGroups(shown = shown, hidden = hidden, mandatoryIds = menuState.mandatoryGroupIds)
        }
    val moveGroupsHandler = remember(store, groups) { MoveGroupsHandler(store = store, groups = groups) }
    return rememberHideableListDragState(
        items = groups,
        listState = listState,
        itemInteractionHandler = moveGroupsHandler,
        fixedItems = FIXED_ITEMS,
        onAttemptedHide = { (it as? String)?.let { id -> store.dispatch(AttemptedRemovalOfMandatorySection(id)) } },
    )
}

/**
 * Allows dragging the [items] shown in the list with [listState] to reorder them, hide them or show them again, letting
 * [itemInteractionHandler] move them when the user lets go of a dragged item. Calls [onAttemptedHide], once for each
 * drag, when an item which cannot be hidden is dragged over the area holding the hidden items. The user feels it
 * whenever the line, or the highlighted area, showing where the dragged item is dropped appears or moves to another
 * place.
 *
 * @param fixedItems Keys of the list items which cannot be dragged but next to which items can be dropped.
 * @param onAttemptedHide Called with the key of the dragged item which cannot be hidden.
 */
@Composable
internal fun <T : HideableListItems> rememberHideableListDragState(
    items: T,
    listState: LazyListState,
    itemInteractionHandler: ItemInteractionHandler,
    fixedItems: Set<Any>,
    onAttemptedHide: (key: Any) -> Unit,
): CustomizeMenuDragState<T> {
    val windowHeight = with(LocalDensity.current) { LocalWindowInfo.current.containerSize.height.toDp() }
    val extraSpace = remember { ExtraItemSpace() }
    val interactionState =
        createListInteractionState(
            listState = listState,
            ignoredItems = setOf(DRAG_BAR, RESET_BUTTON),
            liveReorderEnabled = false,
            itemInteractionHandler = itemInteractionHandler,
            dragAndDropEnabled = false,
            fixedItems = fixedItems,
            // The items differ in height, so a part of the window rather than of the first one shown.
            scrollZoneHeight = windowHeight * AUTO_SCROLL_ZONE_WINDOW_FRACTION,
            interceptInteractionMode = { dragged, mode, layoutInfo ->
                interactionModeOf(dragged, mode, layoutInfo, items)
            },
            // Leave out the drop line's space, or moving the line would move where the item is dropped.
            targetingLayoutInfo = { it.without(extraSpace) },
        )
    val isDragging by
        remember(interactionState) { derivedStateOf { interactionState.draggedItem != InteractionState.List.None } }

    val hasHiddenItems by
        remember(interactionState, items) {
            derivedStateOf { items.hiddenKeys.any { it != interactionState.draggedItem.key } }
        }

    val draggedShownItem = rememberDraggedShownItem(interactionState, items)

    val dropTarget =
        remember(interactionState, items) {
            derivedStateOf {
                val draggedItem = interactionState.draggedItem
                val draggedKey = draggedItem.key ?: return@derivedStateOf null
                // Only once moved, before which the item is still where it was picked up.
                if (!draggedItem.moved) return@derivedStateOf null
                dropTargetOf(draggedKey, interactionState.interactionMode, items)
            }
        }

    DropIndicationHaptics(interactionState, items, dropTarget)

    val currentOnAttemptedHide by rememberUpdatedState(onAttemptedHide)
    val draggedMandatoryKey by
        remember(interactionState, items) {
            derivedStateOf { interactionState.draggedItem.key?.takeIf { items.cannotBeHidden(it) } }
        }
    // Once for each drag, for the user not to be told again while trying again.
    LaunchedEffect(draggedMandatoryKey) {
        val key = draggedMandatoryKey ?: return@LaunchedEffect
        snapshotFlow {
            (interactionState.draggedItem as? InteractionState.List.Active)?.let {
                draggedPositionOf(
                    dragged = it,
                    layoutInfo = listState.layoutInfo.without(extraSpace),
                    items = items,
                )
            } == DraggedPosition.OVER_HIDDEN_AREA
        }
            .first { isOver -> isOver }
        currentOnAttemptedHide(key)
    }

    return CustomizeMenuDragState(
        items = items,
        listState = listState,
        interactionState = interactionState,
        isDragging = isDragging,
        hasHiddenItems = hasHiddenItems,
        dropTarget = dropTarget,
        extraSpace = extraSpace,
        draggedShownItem = draggedShownItem,
    )
}

/** Where the item dragged with [interactionState] was when the drag started, if it was among the shown [items]. */
@Composable
private fun rememberDraggedShownItem(
    interactionState: ListInteractionState,
    items: HideableListItems,
): State<DraggedShownItem?> =
    remember(interactionState, items) {
        derivedStateOf {
            (interactionState.draggedItem as? InteractionState.List.Active)
                ?.takeIf { dragged -> items.isShownKey(dragged.key) }
                ?.let { DraggedShownItem(index = it.index, size = it.size) }
        }
    }

/**
 * Moves the groups of a customize menu screen when the user lets go of a dragged group, saving the move.
 *
 * @param store Store to save the moves to.
 * @param groups The groups as last saved.
 */
internal class MoveGroupsHandler(
    private val store: CustomizableMenuStore,
    private val groups: CustomizeMenuGroups,
) : ItemInteractionHandler {
    // Set only once the group is moved further than the touch slop.
    private var draggedGroupId: Any? = null

    // Without live reordering, the list tells where the dragged group is moved to only when the user lets go of it.
    private var movedGroups: CustomizeMenuGroups? = null

    override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) {
        if (sourceKey != draggedGroupId || targetKey == null) return
        val target =
            if (targetKey == DROP_AREA) {
                CustomizeMenuDropTarget.EmptyHiddenArea
            } else {
                CustomizeMenuDropTarget.NextTo(targetKey, placeAfter)
            }
        movedGroups = groups.drop(sourceKey, target)
    }

    override fun onDrop(sourceKey: Any, targetKey: Any) = Unit

    override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
        draggedGroupId = sourceKey
    }

    override fun onDragEnd() {
        val groupId = draggedGroupId
        val moved = movedGroups
        draggedGroupId = null
        movedGroups = null
        if (groupId is String && moved != null) store.saveMove(groups, groupId, moved)
    }

    override fun onDragCancel() {
        draggedGroupId = null
        movedGroups = null
    }
}
