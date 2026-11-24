/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose.customize

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.coroutineScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.ui.customize.EditMenuItemsContent
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.middleware.EditMenuItemsMiddleware
import org.mozilla.fenix.components.menu.middleware.EditMenuItemsNavigationMiddleware
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.theme.FirefoxTheme

/** Screen allowing the user to customize which items of a menu section are shown and in which order. */
class EditMenuItemsFragment : Fragment(), SystemInsetsPaddedFragment {
    private val args by navArgs<EditMenuItemsFragmentArgs>()

    private val store: CustomizableMenuItemsStore by
        fragmentStore(CustomizableMenuItemsState()) { restoredState ->
            val menuItemsRegistry =
                MenuItemsRegistry(
                    configuration = BrowserMenuConfiguration.allSections().filter { it.id == args.sectionId },
                    resolver = buildCustomizeMenuItemProvidersFactory()::buildProviderFor,
                )

            CustomizableMenuItemsStore(
                initialState = restoredState,
                middleware =
                    listOf(
                        EditMenuItemsMiddleware(
                            repository = requireComponents.menuCustomizationRepository,
                            registry = menuItemsRegistry,
                            sectionId = args.sectionId,
                            scope = viewLifecycleOwner.lifecycle.coroutineScope,
                        ),
                        EditMenuItemsNavigationMiddleware(findNavController()),
                    ),
            )
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        FirefoxTheme {
            EditMenuItemsContent(store = store)
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.edit_menu_items))
    }
}
