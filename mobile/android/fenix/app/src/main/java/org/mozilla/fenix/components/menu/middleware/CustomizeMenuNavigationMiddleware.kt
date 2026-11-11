/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import androidx.navigation.NavController
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.compose.customize.CustomizeMenuFragmentDirections
import org.mozilla.fenix.ext.nav

/**
 * [Middleware] handling navigation for the menu customization screen.
 *
 * @param navController [NavController] executing any navigation actions.
 */
class CustomizeMenuNavigationMiddleware(private val navController: NavController) :
    Middleware<CustomizableMenuState, CustomizableMenuAction> {

    override fun invoke(
        store: Store<CustomizableMenuState, CustomizableMenuAction>,
        next: (CustomizableMenuAction) -> Unit,
        action: CustomizableMenuAction,
    ) {
        next(action)

        when (action) {
            is PreviewClicked -> {
                navController.nav(
                    R.id.customizeMenuFragment,
                    CustomizeMenuFragmentDirections.actionCustomizeMenuFragmentToMenuFragment(
                        accessPoint = MenuAccessPoint.Preview
                    ),
                )
            }

            is ResetToDefaultClicked -> {
                navController.nav(
                    R.id.customizeMenuFragment,
                    CustomizeMenuFragmentDirections.actionCustomizeMenuFragmentToResetToDefaultDialogFragment(),
                )
            }

            is Init,
            is Update,
            is MenuStructureChanged,
            is CustomizationStatusChanged,
            is AttemptedRemovalOfMandatorySection,
            is BackPressed,
            is ResetToDefaultConfirmed,
            is MenuEvent -> Unit
        }
    }
}
