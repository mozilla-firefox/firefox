/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** MVI [Store] for the screen allowing users to customize the menu. */
class CustomizableMenuStore(
    initialState: CustomizableMenuState = CustomizableMenuState(emptyList()),
    middleware: List<Middleware<CustomizableMenuState, CustomizableMenuAction>> = emptyList(),
) :
    Store<CustomizableMenuState, CustomizableMenuAction>(
        initialState = initialState,
        reducer = ::reduce,
        middleware = middleware,
    ) {

    init {
        dispatch(Init)
    }
}

private fun reduce(state: CustomizableMenuState, action: CustomizableMenuAction): CustomizableMenuState =
    when (action) {
        is Update ->
            state.copy(
                menuGroups = action.items,
                mandatoryGroupIds = action.mandatoryGroupIds,
                hiddenGroupIds = action.hiddenGroupIds,
                isCustomized = action.isCustomized,
            )
        is MenuStructureChanged -> state.copy(menuGroups = action.items, hiddenGroupIds = action.hiddenGroupIds)
        is CustomizationStatusChanged -> state.copy(isCustomized = action.isCustomized)
        is Init,
        is MenuEvent -> state
    }
