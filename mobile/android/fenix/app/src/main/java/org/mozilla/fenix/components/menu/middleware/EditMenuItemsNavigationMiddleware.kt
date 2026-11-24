/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import androidx.navigation.NavController
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.compose.customize.EditMenuItemsFragmentDirections
import org.mozilla.fenix.ext.nav

/**
 * [Middleware] handling navigation for the screen customizing the items of a menu section.
 *
 * @param navController [NavController] executing any navigation actions.
 */
class EditMenuItemsNavigationMiddleware(private val navController: NavController) :
    Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> {

    override fun invoke(
        store: Store<CustomizableMenuItemsState, CustomizableMenuItemsAction>,
        next: (CustomizableMenuItemsAction) -> Unit,
        action: CustomizableMenuItemsAction,
    ) {
        next(action)

        when (action) {
            is PreviewClicked -> {
                navController.nav(
                    R.id.editMenuItemsFragment,
                    EditMenuItemsFragmentDirections.actionEditMenuItemsFragmentToMenuFragment(
                        accessPoint = MenuAccessPoint.Preview
                    ),
                )
            }

            is Init,
            is Update,
            is ItemsChanged,
            is ExpansionToggled -> Unit
        }
    }
}
