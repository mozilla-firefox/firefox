/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** MVI [Store] for the screen allowing users to customize the items of a menu section. */
class CustomizableMenuItemsStore(
    initialState: CustomizableMenuItemsState = CustomizableMenuItemsState(),
    middleware: List<Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction>> = emptyList(),
) :
    Store<CustomizableMenuItemsState, CustomizableMenuItemsAction>(
        initialState = initialState,
        reducer = ::reduce,
        middleware = middleware,
    ) {

    init {
        dispatch(Init)
    }
}

private fun reduce(state: CustomizableMenuItemsState, action: CustomizableMenuItemsAction): CustomizableMenuItemsState =
    when (action) {
        is Update -> {
            val isFirstUpdate = state.items.isEmpty() && state.hiddenItems.isEmpty()
            state.copy(
                items = action.items,
                hiddenItems = action.hiddenItems,
                expandedItemIds =
                    if (isFirstUpdate) {
                        action.items.filter { it.item is ExpandableMenuItem }.map { it.id }.toSet()
                    } else {
                        state.expandedItemIds
                    },
            )
        }
        is ItemsChanged -> state.copy(items = action.items, hiddenItems = action.hiddenItems)
        is ExpansionToggled ->
            state.copy(
                expandedItemIds =
                    if (action.itemId in state.expandedItemIds) {
                        state.expandedItemIds - action.itemId
                    } else {
                        state.expandedItemIds + action.itemId
                    }
            )
        is Init,
        is PreviewClicked -> state
    }
