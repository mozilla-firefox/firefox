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
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuContent
import mozilla.components.feature.automotive.isAndroidAutomotiveAvailable
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuItemProvidersFactory
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.middleware.CustomizeMenuMiddleware
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.isToolbarAtBottom
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.translations.TranslationsEnabledSettings

/** Screen allowing the user to customize the order of and what menu items or menu sections are to be shown. */
class CustomizeMenuFragment : Fragment(), SystemInsetsPaddedFragment {
    private val menuStore: CustomizableMenuStore by
        fragmentStore(CustomizableMenuState(emptyList())) { restoredState ->
            val components = requireComponents
            val menuItemsRegistry =
                MenuItemsRegistry(
                    configuration = BrowserMenuConfiguration.allSections(),
                    resolver = buildMenuItemProvidersFactory()::buildProviderFor,
                )

            CustomizableMenuStore(
                initialState = restoredState,
                middleware =
                    listOf(
                        CustomizeMenuMiddleware(
                            repository = components.menuCustomizationRepository,
                            menuBuilder = MenuBuilder(menuItemsRegistry),
                            isToolbarAtBottom = requireContext().isToolbarAtBottom(),
                            isExpandedToolbarEnabled = components.settings.shouldUseExpandedToolbar,
                            scope = viewLifecycleOwner.lifecycle.coroutineScope,
                        )
                    ),
            )
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        FirefoxTheme {
            CustomizeMenuContent(store = menuStore)
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.customize_menu))
    }

    private fun buildMenuItemProvidersFactory(): MenuItemProvidersFactory {
        val components = requireComponents
        val context = requireContext()
        val activity = requireActivity()

        return MenuItemProvidersFactory(
            context = context,
            accessPoint = MenuAccessPoint.Browser,
            target = MenuTarget.CustomizeMenu,
            browserStore = components.core.store,
            appStore = components.appStore,
            ipProtectionStore = components.ipProtection.store,
            syncStore = components.backgroundServices.syncStore,
            httpClient = components.core.client,
            historyStorage = components.core.historyStorage,
            bookmarksStorage = components.core.bookmarksStorage,
            pinnedSiteStorage = components.core.pinnedSiteStorage,
            tabCollectionStorage = components.core.tabCollectionStorage,
            permissionStorage = components.core.permissionStorage,
            useCases = components.useCases,
            addonManager = components.addonManager,
            engine = components.core.engine,
            settings = components.settings,
            summarizeFeatureSettings = components.core.summarizeFeatureSettings,
            summarizeEligibilityChecker = components.core.summarizationEligibilityChecker,
            translationsSettings = TranslationsEnabledSettings.dataStore(context),
            isAndroidAutomotiveAvailable = context.isAndroidAutomotiveAvailable(),
            materialAlertDialogBuilder = MaterialAlertDialogBuilder(context),
            quitApplicationDelegate = activity::finishAndRemoveTask,
            setAsDefaultBrowserDelegate = activity::openSetDefaultBrowserOption,
            menuViewScope = viewLifecycleOwner.lifecycle.coroutineScope,
            applicationScope = components.applicationScope,
        )
    }
}
