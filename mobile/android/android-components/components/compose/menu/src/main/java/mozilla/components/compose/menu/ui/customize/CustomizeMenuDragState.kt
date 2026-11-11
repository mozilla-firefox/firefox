/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.draggable.ListInteractionState
import mozilla.components.compose.draggable.createListInteractionState
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON

/**
 * State of dragging the groups of the customize menu screen.
 *
 * @property groups The shown and the hidden groups.
 * @property interactionState State of dragging the items of the list showing the groups.
 * @property isDragging Whether a group is dragged.
 */
internal class CustomizeMenuDragState(
    val groups: CustomizeMenuGroups,
    val interactionState: ListInteractionState,
    val isDragging: Boolean,
) {
    /**
     * Index in the list of the first hidden group, after the drag bar, the shown groups and the hidden sections title.
     */
    val firstHiddenIndex = groups.shown.size + 2
}

private val FIXED_ITEMS = setOf(HIDDEN_SECTIONS)

private const val AUTO_SCROLL_ZONE_WINDOW_FRACTION = 0.1f

/**
 * Allows dragging the shown groups of the customize menu screen shown in the list with [listState] to reorder them,
 * saving the move to [store] when the user lets go of a dragged group.
 */
@Composable
internal fun rememberCustomizeMenuDragState(
    store: CustomizableMenuStore,
    menuState: CustomizableMenuState,
    listState: LazyListState,
): CustomizeMenuDragState {
    val groups =
        remember(menuState.menuGroups, menuState.mandatoryGroupIds, menuState.hiddenGroupIds) {
            val (hidden, shown) = menuState.menuGroups.partition { it.id in menuState.hiddenGroupIds }
            CustomizeMenuGroups(shown = shown, hidden = hidden, mandatoryIds = menuState.mandatoryGroupIds)
        }
    val windowHeight = with(LocalDensity.current) { LocalWindowInfo.current.containerSize.height.toDp() }
    val interactionState =
        createListInteractionState(
            listState = listState,
            // Only the shown groups can be moved, and only next to each other.
            ignoredItems = remember(groups.hidden) { setOf<Any>(DRAG_BAR, RESET_BUTTON) + groups.hidden.map { it.id } },
            liveReorderEnabled = false,
            itemInteractionHandler = remember(store, groups) { MoveGroupsHandler(store = store, groups = groups) },
            dragAndDropEnabled = false,
            // Groups dropped before it are shown last, and the ones dropped after it are put back where they were.
            fixedItems = FIXED_ITEMS,
            // The groups differ in height, so a part of the window rather than of the first one shown.
            scrollZoneHeight = windowHeight * AUTO_SCROLL_ZONE_WINDOW_FRACTION,
        )
    val isDragging by
        remember(interactionState) { derivedStateOf { interactionState.draggedItem != InteractionState.List.None } }

    return CustomizeMenuDragState(
        groups = groups,
        interactionState = interactionState,
        isDragging = isDragging,
    )
}

/**
 * Moves the shown groups of a customize menu screen when the user lets go of a dragged group, saving the move.
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

    // Without live reordering this is called only once the user lets go of the dragged group.
    override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) {
        if (sourceKey != draggedGroupId) return
        val movedGroups = targetKey?.let { groups.moveGroup(sourceKey, it, placeAfter) } ?: return
        if (movedGroups != groups) {
            store.dispatch(
                MenuStructureChanged(
                    items = movedGroups.shown + movedGroups.hidden,
                    hiddenGroupIds = movedGroups.hidden.map { it.id }.toSet(),
                )
            )
        }
    }

    override fun onDrop(sourceKey: Any, targetKey: Any) = Unit

    override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
        draggedGroupId = sourceKey
    }

    override fun onDragCancel() {
        draggedGroupId = null
    }
}
