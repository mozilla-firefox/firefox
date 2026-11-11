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
import mozilla.components.compose.menu.store.CustomizableMenuAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuContent
import mozilla.components.feature.automotive.isAndroidAutomotiveAvailable
import mozilla.components.lib.state.helpers.StoreProvider.Companion.navBackStackStore
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuItemProvidersFactory
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.middleware.CustomizeMenuMiddleware
import org.mozilla.fenix.components.menu.middleware.CustomizeMenuNavigationMiddleware
import org.mozilla.fenix.components.menu.middleware.CustomizeMenuTelemetryMiddleware
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.isToolbarAtBottom
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.snackbar.SnackbarBinding
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.translations.TranslationsEnabledSettings

/** Screen allowing the user to customize the order of and what menu items or menu sections are to be shown. */
class CustomizeMenuFragment : Fragment(), UserInteractionHandler, SystemInsetsPaddedFragment {
    private val snackbarBinding = ViewBoundFeatureWrapper<SnackbarBinding>()

    private val menuStore: CustomizableMenuStore by lazy {
        val backStackEntry = findNavController().getBackStackEntry(R.id.customize_menu_graph)
        backStackEntry
            .navBackStackStore(CustomizableMenuState(emptyList())) { restoredState ->
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
                                appStore = components.appStore,
                                cannotHideSectionMessage = getString(R.string.customize_menu_cannot_hide_section),
                            ),
                            CustomizeMenuNavigationMiddleware(findNavController()),
                            CustomizeMenuTelemetryMiddleware(),
                        ),
                )
            }
            .value
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

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val components = requireComponents
        snackbarBinding.set(
            feature =
                SnackbarBinding(
                    context = requireContext(),
                    browserStore = components.core.store,
                    appStore = components.appStore,
                    snackbarDelegate = FenixSnackbarDelegate(view),
                    navController = findNavController(),
                    tabsUseCases = components.useCases.tabsUseCases,
                    sendTabUseCases = null,
                    customTabSessionId = null,
                    applicationScope = components.applicationScope,
                ),
            owner = this,
            view = view,
        )
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.customize_menu))
    }

    override fun onBackPressed(): Boolean {
        menuStore.dispatch(BackPressed)
        return false
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
            quitApplicationDelegate = activity::finishAndRemoveTask,
            setAsDefaultBrowserDelegate = activity::openSetDefaultBrowserOption,
            menuViewScope = viewLifecycleOwner.lifecycle.coroutineScope,
            applicationScope = components.applicationScope,
            browsingModeManager = (activity as HomeActivity).browsingModeManager,
        )
    }
}
