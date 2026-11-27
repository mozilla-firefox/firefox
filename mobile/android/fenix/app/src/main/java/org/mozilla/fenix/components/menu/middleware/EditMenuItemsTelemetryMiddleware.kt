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
        val isFirstRead = store.state.items.isEmpty() && store.state.hiddenItems.isEmpty()

        next(action)

        when (action) {
            is Update -> if (isFirstRead) recordShown(action)
            is PreviewClicked ->
                CustomizeMenu.editItemsPreviewClicked.record(
                    CustomizeMenu.EditItemsPreviewClickedExtra(section = sectionId)
                )
            is BackPressed ->
                CustomizeMenu.editItemsBackPressed.record(CustomizeMenu.EditItemsBackPressedExtra(section = sectionId))
            is Init,
            is ItemsChanged,
            is ExpansionToggled,
            is AttemptedMoveAsSubItem,
            is AttemptedEmptySection -> Unit
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
}
