/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedMoveAsSubItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA

/**
 * Moves the items of a screen customizing the items of a menu section when the user lets go of a dragged item, saving
 * the move. A drop which is rejected is reported to the store.
 *
 * @param store Store to save the moves to.
 * @param items The items as last saved.
 */
internal class MoveItemsHandler(
    private val store: CustomizableMenuItemsStore,
    private val items: CustomizeMenuItems,
) : ItemInteractionHandler {
    // Set only once the item is moved further than the touch slop.
    private var draggedItemId: Any? = null

    // Without live reordering, the list tells where the dragged item is moved to only when the user lets go of it.
    private var dropTarget: CustomizeMenuDropTarget? = null

    override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) {
        if (sourceKey != draggedItemId || targetKey == null) return
        val target =
            if (targetKey == DROP_AREA) {
                CustomizeMenuDropTarget.EmptyHiddenArea
            } else {
                CustomizeMenuDropTarget.NextTo(targetKey, placeAfter)
            }
        dropTarget = target
    }

    override fun onDrop(sourceKey: Any, targetKey: Any) = Unit

    override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
        draggedItemId = sourceKey
    }

    override fun onDragEnd() {
        val itemId = draggedItemId
        val target = dropTarget
        onDragCancel()
        if (itemId == null || target == null) return
        when (val outcome = items.dropOutcome(itemId, target)) {
            is DropOutcome.Moved ->
                store.dispatch(ItemsChanged(items = outcome.items.shown, hiddenItems = outcome.items.hidden))
            DropOutcome.RejectedAsSubItem -> if (itemId is String) store.dispatch(AttemptedMoveAsSubItem(itemId))
            DropOutcome.Unchanged -> Unit
        }
    }

    override fun onDragCancel() {
        draggedItemId = null
        dropTarget = null
    }
}
