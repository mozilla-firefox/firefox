/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import kotlinx.coroutines.flow.first
import mozilla.components.compose.draggable.InteractionMode
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.draggable.ListInteractionState
import mozilla.components.compose.draggable.createListInteractionState
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
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
 * @property hiddenArea Where the area holding the hidden groups is shown.
 * @property isDragging Whether a group is dragged.
 * @property hasHiddenGroups Whether groups other than the dragged one are hidden. Otherwise the area to drag groups
 *   into to hide them is shown.
 * @property dropTarget Where the dragged group is moved to if the user lets go of it, if anywhere.
 */
internal class CustomizeMenuDragState(
    val groups: CustomizeMenuGroups,
    val interactionState: ListInteractionState,
    val hiddenArea: CustomizeMenuHiddenArea,
    val isDragging: Boolean,
    val hasHiddenGroups: Boolean,
    val dropTarget: State<CustomizeMenuDropTarget?>,
) {
    /**
     * Index in the list of the first hidden group, after the drag bar, the shown groups and the hidden sections title.
     */
    val firstHiddenIndex = hiddenSectionsIndexOf(groups) + 1
}

/** Index in the list of the hidden sections title, after the drag bar and the shown groups. */
private fun hiddenSectionsIndexOf(groups: CustomizeMenuGroups) = 1 + groups.shown.size

// Groups can be dropped next to these items.
private val FIXED_ITEMS = setOf(HIDDEN_SECTIONS)

private const val AUTO_SCROLL_ZONE_WINDOW_FRACTION = 0.1f

/**
 * Allows dragging the groups of the customize menu screen shown in the list with [listState] to reorder them, hide them
 * or show them again, saving the move to [store] when the user lets go of a dragged group. Tells [store], once for each
 * drag, when a group which cannot be removed from the menu is dragged over the area holding the hidden groups.
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
    val moveGroupsHandler = remember(store, groups) { MoveGroupsHandler(store = store, groups = groups) }
    val interactionState =
        createListInteractionState(
            listState = listState,
            ignoredItems = setOf(DRAG_BAR, RESET_BUTTON),
            liveReorderEnabled = false,
            itemInteractionHandler = moveGroupsHandler,
            dragAndDropEnabled = false,
            fixedItems = FIXED_ITEMS,
            // The groups differ in height, so a part of the window rather than of the first one shown.
            scrollZoneHeight = windowHeight * AUTO_SCROLL_ZONE_WINDOW_FRACTION,
        )
    val isDragging by
        remember(interactionState) { derivedStateOf { interactionState.draggedItem != InteractionState.List.None } }

    val hasHiddenGroups by
        remember(interactionState, groups) {
            derivedStateOf { groups.hidden.any { it.id != interactionState.draggedItem.key } }
        }

    val hiddenArea = remember { CustomizeMenuHiddenArea() }
    val hiddenSectionsIndex = hiddenSectionsIndexOf(groups)
    val draggedGroupPosition by
        remember(listState, interactionState, hiddenSectionsIndex, groups) {
            derivedStateOf {
                (interactionState.draggedItem as? InteractionState.List.Active)?.let {
                    // Dragged out of them, the last hidden group is still laid out below the area to drag groups into.
                    hiddenArea.positionOf(it, listState.layoutInfo, hiddenSectionsIndex, groups.hidden.isNotEmpty())
                }
            }
        }
    val dropTarget =
        remember(listState, interactionState, groups) {
            derivedStateOf {
                val draggedItem = interactionState.draggedItem
                val draggedGroupId = draggedItem.key ?: return@derivedStateOf null
                // Only once moved, before which the group is still where it was picked up.
                if (!draggedItem.moved) return@derivedStateOf null
                dropTargetOf(
                    draggedGroupId = draggedGroupId,
                    reordering = interactionState.interactionMode as? InteractionMode.List.Reordering,
                    position = draggedGroupPosition,
                    groups = groups,
                    hasHiddenGroups = hasHiddenGroups,
                )
            }
        }
    SideEffect { moveGroupsHandler.dropTarget = { dropTarget.value } }

    val draggedMandatoryGroupId by
        remember(interactionState, groups) {
            derivedStateOf { (interactionState.draggedItem.key as? String)?.takeIf { it in groups.mandatoryIds } }
        }
    // Once for each drag, for the user not to be told again while trying again.
    LaunchedEffect(store, draggedMandatoryGroupId) {
        val groupId = draggedMandatoryGroupId ?: return@LaunchedEffect
        snapshotFlow { draggedGroupPosition == DraggedGroupPosition.OVER_HIDDEN_AREA }.first { isOver -> isOver }
        store.dispatch(AttemptedRemovalOfMandatorySection(groupId))
    }

    return CustomizeMenuDragState(
        groups = groups,
        interactionState = interactionState,
        hiddenArea = hiddenArea,
        isDragging = isDragging,
        hasHiddenGroups = hasHiddenGroups,
        dropTarget = dropTarget,
    )
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
    /** Where the dragged group is moved to if the user lets go of it, if anywhere. */
    var dropTarget: () -> CustomizeMenuDropTarget? = { null }

    // Set only once the group is moved further than the touch slop.
    private var draggedGroupId: Any? = null

    // The dragged group is moved only when the user lets go of it.
    override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) = Unit

    override fun onDrop(sourceKey: Any, targetKey: Any) = Unit

    override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
        draggedGroupId = sourceKey
    }

    override fun onDragEnd() {
        val groupId = draggedGroupId ?: return
        draggedGroupId = null
        dropTarget()?.let { save(groups.drop(groupId, it)) }
    }

    override fun onDragCancel() {
        draggedGroupId = null
    }

    private fun save(movedGroups: CustomizeMenuGroups) {
        if (movedGroups != groups) {
            store.dispatch(
                MenuStructureChanged(
                    items = movedGroups.shown + movedGroups.hidden,
                    hiddenGroupIds = movedGroups.hidden.map { it.id }.toSet(),
                )
            )
        }
    }
}
