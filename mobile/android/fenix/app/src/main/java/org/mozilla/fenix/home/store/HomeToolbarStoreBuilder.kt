/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.store

import android.content.Context
import androidx.fragment.app.Fragment
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.coroutineScope
import androidx.navigation.NavController
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isTallWindow
import org.mozilla.fenix.ext.isWideWindow
import org.mozilla.fenix.home.privatebrowsing.controller.DefaultPrivateBrowsingController
import org.mozilla.fenix.home.toolbar.BrowserToolbarMiddleware
import org.mozilla.fenix.home.toolbar.BrowserToolbarTelemetryMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchStatusSyncMiddleware
import org.mozilla.fenix.translations.TranslationsEnabledSettings

/** Delegate for building the [BrowserToolbarStore] used in the home screen. */
object HomeToolbarStoreBuilder {
    /**
     * Build the [BrowserToolbarStore] used in the home screen.
     *
     * @param context [Context] used for various system interactions.
     * @param fragment [Fragment] as a [LifecycleOwner] to used to organize lifecycle dependent operations.
     * @param navController [NavController] to use for navigating to other in-app destinations.
     * @param appStore [AppStore] to sync from.
     * @param browserStore [BrowserStore] to sync from.
     * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
     */
    fun build(
        context: Context,
        fragment: Fragment,
        navController: NavController,
        appStore: AppStore,
        browserStore: BrowserStore,
        browsingModeManager: BrowsingModeManager,
    ) =
        fragment.fragmentStore(BrowserToolbarState()) {
            val lifecycleScope = fragment.viewLifecycleOwner.lifecycle.coroutineScope

            BrowserToolbarStore(
                initialState = it,
                middleware =
                    listOf(
                        BrowserToolbarSearchStatusSyncMiddleware(
                            appStore = appStore,
                            browsingModeManager = browsingModeManager,
                            scope = lifecycleScope,
                        ),
                        BrowserToolbarMiddleware(
                            uiContext = context,
                            appStore = appStore,
                            browserStore = browserStore,
                            clipboard = context.components.clipboardHandler,
                            fenixBrowserUseCases = context.components.useCases.fenixBrowserUseCases,
                            navController = navController,
                            browsingModeManager = browsingModeManager,
                            privateBrowsingController =
                                DefaultPrivateBrowsingController(
                                    navController = navController,
                                    browsingModeManager = browsingModeManager,
                                    fenixBrowserUseCases = context.components.useCases.fenixBrowserUseCases,
                                    settings = context.components.settings,
                                ),
                            settings = context.components.settings,
                            translationsFeatureSettings = TranslationsEnabledSettings.dataStore(context),
                            menuCustomizationRepository =
                                if (
                                    context.components.settings.isMenuCustomizationScreenEnabled ||
                                        context.components.settings.isToolsMenuCustomizationEnabled
                                ) {
                                    context.components.menuCustomizationRepository
                                } else {
                                    null
                                },
                            isWideScreen = { fragment.isWideWindow() },
                            isTallScreen = { fragment.isTallWindow() },
                            scope = lifecycleScope,
                        ),
                        BrowserToolbarSearchMiddleware(
                            uiContext = context,
                            appStore = appStore,
                            browserStore = browserStore,
                            components = context.components,
                            navController = navController,
                            browsingModeManager = browsingModeManager,
                            settings = context.components.settings,
                            scope = lifecycleScope,
                        ),
                        BrowserToolbarTelemetryMiddleware(
                            appStore = appStore,
                            settings = context.components.settings,
                        ),
                    ),
            )
        }
}
