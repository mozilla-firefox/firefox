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
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalWindowInfo
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.draggable.ListInteractionState
import mozilla.components.compose.draggable.createListInteractionState
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON

/**
 * State of dragging the groups of the customize menu screen.
 *
 * @property groups The shown and the hidden groups.
 * @property listState State of the list showing the groups.
 * @property interactionState State of dragging the items of the list showing the groups.
 * @property isDragging Whether a group is dragged.
 * @property hasHiddenGroups Whether groups other than the dragged one are hidden. Otherwise the area to drag groups
 *   into to hide them is shown.
 * @property dropTarget Where the dragged group is moved to if the user lets go of it, if anywhere.
 */
internal class CustomizeMenuDragState(
    val groups: CustomizeMenuGroups,
    val listState: LazyListState,
    val interactionState: ListInteractionState,
    val isDragging: Boolean,
    val hasHiddenGroups: Boolean,
    val dropTarget: State<CustomizeMenuDropTarget?>,
) {
    /**
     * Index in the list of the first hidden group, after the drag bar, the shown groups, the hidden sections title and
     * the area to drag groups into if shown.
     */
    val firstHiddenIndex = groups.hiddenSectionsIndex + if (hasHiddenGroups) 1 else 2
}

// Groups can be dropped next to these items.
private val FIXED_ITEMS = setOf(HIDDEN_SECTIONS, DROP_AREA)

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
            interceptInteractionMode = { dragged, mode ->
                interactionModeOf(dragged, mode, listState.layoutInfo, groups)
            },
        )
    val isDragging by
        remember(interactionState) { derivedStateOf { interactionState.draggedItem != InteractionState.List.None } }

    val hasHiddenGroups by
        remember(interactionState, groups) {
            derivedStateOf { groups.hidden.any { it.id != interactionState.draggedItem.key } }
        }

    val dropTarget =
        remember(interactionState, groups) {
            derivedStateOf {
                val draggedItem = interactionState.draggedItem
                val draggedGroupId = draggedItem.key ?: return@derivedStateOf null
                // Only once moved, before which the group is still where it was picked up.
                if (!draggedItem.moved) return@derivedStateOf null
                dropTargetOf(draggedGroupId, interactionState.interactionMode, groups)
            }
        }

    DropIndicationHaptics(interactionState, groups, dropTarget)

    val draggedMandatoryGroupId by
        remember(interactionState, groups) {
            derivedStateOf { (interactionState.draggedItem.key as? String)?.takeIf { it in groups.mandatoryIds } }
        }
    // Once for each drag, for the user not to be told again while trying again.
    LaunchedEffect(store, draggedMandatoryGroupId) {
        val groupId = draggedMandatoryGroupId ?: return@LaunchedEffect
        snapshotFlow {
            (interactionState.draggedItem as? InteractionState.List.Active)?.let {
                draggedGroupPositionOf(dragged = it, layoutInfo = listState.layoutInfo, groups = groups)
            } == DraggedGroupPosition.OVER_HIDDEN_AREA
        }
            .first { isOver -> isOver }
        store.dispatch(AttemptedRemovalOfMandatorySection(groupId))
    }

    return CustomizeMenuDragState(
        groups = groups,
        listState = listState,
        interactionState = interactionState,
        isDragging = isDragging,
        hasHiddenGroups = hasHiddenGroups,
        dropTarget = dropTarget,
    )
}

/**
 * Lets the user feel it whenever the line, or the highlighted area, showing where the group dragged with
 * [interactionState] is dropped appears or moves to another place.
 *
 * @param interactionState State of dragging the items of the list showing the groups.
 * @param groups The groups as last saved.
 * @param dropTarget Where the dragged group is moved to if the user lets go of it, if anywhere.
 */
@Composable
private fun DropIndicationHaptics(
    interactionState: ListInteractionState,
    groups: CustomizeMenuGroups,
    dropTarget: State<CustomizeMenuDropTarget?>,
) {
    val hapticFeedback = LocalHapticFeedback.current
    LaunchedEffect(interactionState, groups, dropTarget) {
        // Different drop targets can show the line at the same place, which is felt only once.
        snapshotFlow {
            val draggedGroupId = interactionState.draggedItem.key ?: return@snapshotFlow null
            dropTarget.value?.let { groups.drop(draggedGroupId, it) }
        }
            .filterNotNull()
            .collect { hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress) }
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
        if (groupId is String && moved != null) save(groupId, moved)
    }

    override fun onDragCancel() {
        draggedGroupId = null
        movedGroups = null
    }

    private fun save(movedGroupId: String, movedGroups: CustomizeMenuGroups) {
        if (movedGroups != groups) {
            store.dispatch(
                MenuStructureChanged(
                    items = movedGroups.shown + movedGroups.hidden,
                    hiddenGroupIds = movedGroups.hidden.map { it.id }.toSet(),
                    movedGroupId = movedGroupId,
                )
            )
        }
    }
}
