/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedEmptySection
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedMoveAsSubItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.GleanMetrics.CustomizeMenu

private const val ITEMS_SEPARATOR = ","
private const val REASON_INSIDE_ITEM = "inside_item"
private const val REASON_LAST_SHOWN_ITEM = "last_shown_item"

/**
 * [CustomizableMenuItemsStore] middleware recording telemetry for how users interact with the screen customizing the
 * items of a menu section.
 *
 * @param sectionId Id of the menu section whose items are customized.
 */
class EditMenuItemsTelemetryMiddleware(private val sectionId: String) :
    Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> {
    override fun invoke(
        store: Store<CustomizableMenuItemsState, CustomizableMenuItemsAction>,
        next: (CustomizableMenuItemsAction) -> Unit,
        action: CustomizableMenuItemsAction,
    ) {
        val before = store.state
        val isFirstRead = before.items.isEmpty() && before.hiddenItems.isEmpty()

        next(action)

        when (action) {
            is Update -> if (isFirstRead) recordShown(action)
            is ItemsChanged -> recordItemMoved(before, action)
            is PreviewClicked ->
                CustomizeMenu.editItemsPreviewClicked.record(
                    CustomizeMenu.EditItemsPreviewClickedExtra(section = sectionId)
                )
            is BackPressed ->
                CustomizeMenu.editItemsBackPressed.record(CustomizeMenu.EditItemsBackPressedExtra(section = sectionId))
            is AttemptedMoveAsSubItem ->
                CustomizeMenu.editItemsMoveRefused.record(
                    CustomizeMenu.EditItemsMoveRefusedExtra(
                        section = sectionId,
                        item = action.itemId,
                        reason = REASON_INSIDE_ITEM,
                    )
                )
            is AttemptedEmptySection ->
                CustomizeMenu.editItemsMoveRefused.record(
                    CustomizeMenu.EditItemsMoveRefusedExtra(
                        section = sectionId,
                        item = action.itemId,
                        reason = REASON_LAST_SHOWN_ITEM,
                    )
                )
            is Init,
            is ExpansionToggled -> Unit
        }
    }

    private fun recordItemMoved(before: CustomizableMenuItemsState, change: ItemsChanged) {
        val movedId = change.movedItemId
        val from = placeOf(movedId, before.items, before.hiddenItems) ?: return
        val to = placeOf(movedId, change.items, change.hiddenItems) ?: return

        when {
            from.isHidden == to.isHidden ->
                CustomizeMenu.itemReordered.record(
                    CustomizeMenu.ItemReorderedExtra(
                        section = sectionId,
                        item = movedId,
                        fromContainer = from.containerId,
                        toContainer = to.containerId,
                        position = to.position,
                        hidden = to.isHidden,
                    )
                )
            !from.isHidden && to.isHidden ->
                CustomizeMenu.itemHidden.record(
                    CustomizeMenu.ItemHiddenExtra(
                        section = sectionId,
                        item = movedId,
                        fromContainer = from.containerId,
                        toContainer = to.containerId,
                        position = to.position,
                    )
                )
            else ->
                CustomizeMenu.itemUnhidden.record(
                    CustomizeMenu.ItemUnhiddenExtra(
                        section = sectionId,
                        item = movedId,
                        fromContainer = from.containerId,
                        toContainer = to.containerId,
                        position = to.position,
                    )
                )
        }
    }

    private fun placeOf(
        id: String,
        items: List<CustomizableMenuItem>,
        hiddenItems: List<CustomizableMenuItem>,
    ): Place? {
        val shown = shownItems(items)
        val shownIndex = shown.indexOfFirst { it.id == id }
        if (shownIndex != -1) {
            val containerId =
                items
                    .firstOrNull { parent ->
                        parent.item != null && parent.subItems.any { it.item != null && it.id == id }
                    }
                    ?.id ?: sectionId
            return Place(containerId = containerId, isHidden = false, position = shownIndex + 1)
        }

        val hidden = hiddenItems(hiddenItems)
        val hiddenIndex = hidden.indexOfFirst { it.id == id }
        return if (hiddenIndex != -1) {
            Place(containerId = sectionId, isHidden = true, position = hiddenIndex + 1)
        } else {
            hidden.firstNotNullOfOrNull { parent ->
                val subIndex = parent.subItems.filter { it.item != null }.indexOfFirst { it.id == id }
                if (subIndex != -1) {
                    Place(containerId = parent.id, isHidden = true, position = subIndex + 1)
                } else {
                    null
                }
            }
        }
    }

    private fun recordShown(update: Update) {
        val moreItems =
            update.items.filter { it.item is ExpandableMenuItem }.flatMap { it.subItems }.filter { it.item != null }

        CustomizeMenu.editItemsShown.record(
            CustomizeMenu.EditItemsShownExtra(
                section = sectionId,
                shownItems = shownItems(update.items).joinToString(ITEMS_SEPARATOR) { it.id },
                hiddenItems = hiddenItems(update.hiddenItems).joinToString(ITEMS_SEPARATOR) { it.id },
                moreItems = moreItems.joinToString(ITEMS_SEPARATOR) { it.id },
            )
        )
    }

    private fun shownItems(items: List<CustomizableMenuItem>): List<CustomizableMenuItem> =
        items
            .filter { it.item != null }
            .flatMap { parent ->
                listOf(parent) + parent.subItems.filter { it.item != null }
            }

    private fun hiddenItems(items: List<CustomizableMenuItem>): List<CustomizableMenuItem> = items.filter {
        it.item != null
    }

    private class Place(val containerId: String, val isHidden: Boolean, val position: Int)
}
