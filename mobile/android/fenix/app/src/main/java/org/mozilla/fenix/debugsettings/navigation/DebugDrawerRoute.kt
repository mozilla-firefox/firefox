/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.navigation

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.LoginsStorage
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.addons.ui.AddonsDebugToolsScreen
import org.mozilla.fenix.debugsettings.addresses.AddressesDebugRegionRepository
import org.mozilla.fenix.debugsettings.addresses.AddressesTools
import org.mozilla.fenix.debugsettings.autofill.AutofillTools
import org.mozilla.fenix.debugsettings.cfrs.CfrToolsState
import org.mozilla.fenix.debugsettings.cfrs.CfrToolsStore
import org.mozilla.fenix.debugsettings.crashtools.CrashTools
import org.mozilla.fenix.debugsettings.creditcards.CreditCardsTools
import org.mozilla.fenix.debugsettings.gleandebugtools.GleanDebugToolsStore
import org.mozilla.fenix.debugsettings.gleandebugtools.ui.GleanDebugToolsScreen
import org.mozilla.fenix.debugsettings.integrity.IntegrityTools
import org.mozilla.fenix.debugsettings.logins.LoginsTools
import org.mozilla.fenix.debugsettings.region.RegionTools
import org.mozilla.fenix.debugsettings.store.DebugDrawerAction
import org.mozilla.fenix.debugsettings.store.DebugDrawerStore
import org.mozilla.fenix.debugsettings.cfrs.CfrTools as CfrToolsScreen
import org.mozilla.fenix.debugsettings.tabs.TabTools as TabToolsScreen

/**
 * The navigation routes for screens within the Debug Drawer.
 *
 * @property route The unique route used to navigate to the destination. This string can also contain
 * optional parameters for arguments or deep linking.
 * @property title The string ID of the destination's title.
 */
enum class DebugDrawerRoute(
    val route: String,
    @param:StringRes val title: Int,
) {
    /**
     * The navigation route for [TabToolsScreen].
     */
    TabTools(
        route = "tab_tools",
        title = R.string.debug_drawer_tab_tools_title,
    ),
    Logins(
        route = "logins",
        title = R.string.debug_drawer_logins_title,
    ),
    Addresses(
        route = "addresses",
        title = R.string.debug_drawer_addresses_title,
    ),
    CreditCards(
        route = "credit_cards",
        title = R.string.debug_drawer_credit_cards_title,
    ),
    Autofill(
        route = "autofill",
        title = R.string.debug_drawer_autofill_title,
    ),
    CfrTools(
        route = "cfr_tools",
        title = R.string.debug_drawer_cfr_tools_title,
    ),
    GleanDebugTools(
        route = "glean_debug_tools",
        title = R.string.glean_debug_tools_title,
    ),
    RegionDebugTools(
        route = "region_debug_tools",
        title = R.string.debug_drawer_region_tools_title,
    ),
    AddonsDebugTools(
        route = "addons_debug_tools",
        title = R.string.debug_drawer_addons_tools_title,
    ),
    CrashDebugTools(
        route = "crash_debug_tools",
        title = R.string.crash_debug_tools_title,
    ),
    IntegrityTools(
        route = "integrity_tools",
        title = R.string.integrity_debug_tools_title,
    ),
    ;

    companion object {
        /**
         * Transforms the values of [DebugDrawerRoute] into a list of [DebugDrawerDestination]s.
         *
         * @param debugDrawerStore [DebugDrawerStore] used to dispatch navigation actions.
         * @param browserStore [BrowserStore] used to access [BrowserState].
         * @param cfrToolsStore [CfrToolsStore] used to access [CfrToolsState].
         * @param gleanDebugToolsStore [GleanDebugToolsStore] used to dispatch glean debug tools actions.
         * @param loginsStorage [LoginsStorage] used to access logins for [LoginsScreen].
         * @param addressesDebugRegionRepository used to control storage for [AddressesTools].
         * @param creditCardsAddressesStorage used to access addresses for [AddressesTools].
         * @param integrityClient used to test an [IntegrityClient] in [IntegrityTools].
         * @param inactiveTabsEnabled Whether the inactive tabs feature is enabled.
         */
        @Suppress("LongParameterList", "LongMethod")
        fun generateDebugDrawerDestinations(
            debugDrawerStore: DebugDrawerStore,
            browserStore: BrowserStore,
            cfrToolsStore: CfrToolsStore,
            gleanDebugToolsStore: GleanDebugToolsStore,
            loginsStorage: LoginsStorage,
            addressesDebugRegionRepository: AddressesDebugRegionRepository,
            creditCardsAddressesStorage: CreditCardsAddressesStorage,
            integrityClient: IntegrityClient,
            inactiveTabsEnabled: Boolean,
        ): List<DebugDrawerDestination> =
            entries.map { debugDrawerRoute ->
                var isChildDestination: Boolean = false
                val onClick: () -> Unit
                val content: @Composable () -> Unit
                when (debugDrawerRoute) {
                    TabTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.TabTools)
                        }
                        content = {
                            TabToolsScreen(
                                store = browserStore,
                                inactiveTabsEnabled = inactiveTabsEnabled,
                            )
                        }
                    }

                    Logins -> {
                        isChildDestination = true
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.Logins)
                        }
                        content = {
                            LoginsTools(
                                browserStore = browserStore,
                                loginsStorage = loginsStorage,
                            )
                        }
                    }

                    Addresses -> {
                        isChildDestination = true
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.Addresses)
                        }
                        content = {
                            AddressesTools(
                                debugRegionRepository = addressesDebugRegionRepository,
                                creditCardsAddressesStorage = creditCardsAddressesStorage,
                            )
                        }
                    }

                    CreditCards -> {
                        isChildDestination = true
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.CreditCards)
                        }
                        content = {
                            CreditCardsTools(
                                creditCardsAddressesStorage = creditCardsAddressesStorage,
                            )
                        }
                    }
                    Autofill -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.Autofill)
                        }
                        content = {
                            AutofillTools(
                                debugDrawerStore = debugDrawerStore,
                            )
                        }
                    }

                    CfrTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.CfrTools)
                        }
                        content = {
                            CfrToolsScreen(cfrToolsStore = cfrToolsStore)
                        }
                    }

                    GleanDebugTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.GleanDebugTools)
                        }
                        content = {
                            GleanDebugToolsScreen(gleanDebugToolsStore = gleanDebugToolsStore)
                        }
                    }

                    RegionDebugTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.RegionDebugTools)
                        }
                        content = {
                            RegionTools(
                                browserStore = browserStore,
                            )
                        }
                    }

                    AddonsDebugTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.AddonsDebugTools)
                        }
                        content = {
                            AddonsDebugToolsScreen()
                        }
                    }

                    CrashDebugTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.CrashDebugTools)
                        }
                        content = {
                            CrashTools()
                        }
                    }
                    IntegrityTools -> {
                        onClick = {
                            debugDrawerStore.dispatch(DebugDrawerAction.NavigateTo.IntegrityDebugTools)
                        }
                        content = {
                            IntegrityTools(integrityClient)
                        }
                    }
                }

                DebugDrawerDestination(
                    route = debugDrawerRoute.route,
                    title = debugDrawerRoute.title,
                    isChildDestination = isChildDestination,
                    onClick = onClick,
                    content = content,
                )
            }
    }
}
