/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.ui

import android.content.Intent
import android.os.StrictMode
import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.core.net.toUri
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.lifecycleScope
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.LoginsStorage
import mozilla.telemetry.glean.Glean
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.addresses.AddressesDebugRegionRepository
import org.mozilla.fenix.debugsettings.addresses.AddressesTools
import org.mozilla.fenix.debugsettings.addresses.FakeAddressesDebugRegionRepository
import org.mozilla.fenix.debugsettings.addresses.FakeCreditCardsAddressesStorage
import org.mozilla.fenix.debugsettings.addresses.SharedPrefsAddressesDebugRegionRepository
import org.mozilla.fenix.debugsettings.cfrs.CfrToolsPreferencesMiddleware
import org.mozilla.fenix.debugsettings.cfrs.CfrToolsState
import org.mozilla.fenix.debugsettings.cfrs.CfrToolsStore
import org.mozilla.fenix.debugsettings.cfrs.DefaultCfrPreferencesRepository
import org.mozilla.fenix.debugsettings.gleandebugtools.DefaultGleanDebugToolsStorage
import org.mozilla.fenix.debugsettings.gleandebugtools.GleanDebugToolsMiddleware
import org.mozilla.fenix.debugsettings.gleandebugtools.GleanDebugToolsState
import org.mozilla.fenix.debugsettings.gleandebugtools.GleanDebugToolsStore
import org.mozilla.fenix.debugsettings.logins.FakeLoginsStorage
import org.mozilla.fenix.debugsettings.logins.LoginsTools
import org.mozilla.fenix.debugsettings.navigation.DebugDrawerRoute
import org.mozilla.fenix.debugsettings.store.DebugDrawerAction
import org.mozilla.fenix.debugsettings.store.DebugDrawerNavigationMiddleware
import org.mozilla.fenix.debugsettings.store.DebugDrawerStore
import org.mozilla.fenix.debugsettings.store.DrawerStatus
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.theme.DefaultThemeProvider
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Overlay for presenting Fenix-wide debugging content.
 *
 * @param browserStore [BrowserStore] used to access [BrowserState].
 * @param loginsStorage [LoginsStorage] used to access logins for [LoginsTools].
 * @param inactiveTabsEnabled Whether the inactive tabs feature is enabled.
 */
@Composable
fun FenixOverlay(
    browserStore: BrowserStore,
    loginsStorage: LoginsStorage,
    inactiveTabsEnabled: Boolean,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    FenixOverlay(
        browserStore = browserStore,
        cfrToolsStore = CfrToolsStore(
            middlewares = listOf(
                CfrToolsPreferencesMiddleware(
                    cfrPreferencesRepository = DefaultCfrPreferencesRepository(
                        context = LocalContext.current,
                        lifecycleOwner = lifecycleOwner,
                        coroutineScope = lifecycleOwner.lifecycleScope,
                    ),
                    coroutineScope = lifecycleOwner.lifecycleScope,
                ),
            ),
        ),
        gleanDebugToolsStore = GleanDebugToolsStore(
            initialState = GleanDebugToolsState(
                logPingsToConsoleEnabled = Glean.getLogPings(),
                debugViewTag = Glean.getDebugViewTag() ?: "",
            ),
            middlewares = listOf(
                GleanDebugToolsMiddleware(
                    gleanDebugToolsStorage = DefaultGleanDebugToolsStorage(),
                    clipboardHandler = context.components.clipboardHandler,
                    openDebugView = { debugViewLink ->
                        val intent = Intent(Intent.ACTION_VIEW)
                        intent.data = debugViewLink.toUri()
                        context.startActivity(intent)
                    },
                    showToast = stringResource(R.string.glean_debug_tools_send_ping_toast_message).let { template ->
                        { pingType: String ->
                            Toast.makeText(context, template.format(pingType), Toast.LENGTH_LONG).show()
                        }
                    },
                ),
            ),
        ),
        loginsStorage = loginsStorage,
        addressesDebugRegionRepository =
            context.components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                SharedPrefsAddressesDebugRegionRepository(context)
            },
        creditCardsAddressesStorage = context.components.core.autofillStorage,
        inactiveTabsEnabled = inactiveTabsEnabled,
        integrityClient = context.components.integrityClient,
    )
}

/**
 * Overlay for presenting Fenix-wide debugging content.
 *
 * @param browserStore [BrowserStore] used to access [BrowserState].
 * @param cfrToolsStore [CfrToolsStore] used to access [CfrToolsState].
 * @param gleanDebugToolsStore [GleanDebugToolsStore] used to access [GleanDebugToolsState].
 * @param loginsStorage [LoginsStorage] used to access logins for [LoginsTools].
 * @param addressesDebugRegionRepository used to control storage for [AddressesTools].
 * @param creditCardsAddressesStorage used to access addresses for [AddressesTools].
 * @param integrityClient used to test an [IntegrityClient].
 * @param inactiveTabsEnabled Whether the inactive tabs feature is enabled.
 */
@Suppress("LongParameterList")
@Composable
private fun FenixOverlay(
    browserStore: BrowserStore,
    cfrToolsStore: CfrToolsStore,
    gleanDebugToolsStore: GleanDebugToolsStore,
    loginsStorage: LoginsStorage,
    addressesDebugRegionRepository: AddressesDebugRegionRepository,
    creditCardsAddressesStorage: CreditCardsAddressesStorage,
    integrityClient: IntegrityClient,
    inactiveTabsEnabled: Boolean,
) {
    val navController = rememberNavController()
    val coroutineScope = rememberCoroutineScope()

    val debugDrawerStore = remember {
        DebugDrawerStore(
            middlewares = listOf(
                DebugDrawerNavigationMiddleware(
                    navController = navController,
                    scope = coroutineScope,
                ),
            ),
        )
    }

    val debugDrawerDestinations = remember {
        DebugDrawerRoute.generateDebugDrawerDestinations(
            debugDrawerStore = debugDrawerStore,
            browserStore = browserStore,
            cfrToolsStore = cfrToolsStore,
            gleanDebugToolsStore = gleanDebugToolsStore,
            inactiveTabsEnabled = inactiveTabsEnabled,
            loginsStorage = loginsStorage,
            addressesDebugRegionRepository = addressesDebugRegionRepository,
            creditCardsAddressesStorage = creditCardsAddressesStorage,
            integrityClient = integrityClient,
        )
    }
    val drawerStatus by remember {
        debugDrawerStore.stateFlow.map { state -> state.drawerStatus }
    }.collectAsState(initial = DrawerStatus.Closed)

    FirefoxTheme(theme = DefaultThemeProvider.provideTheme()) {
        DebugOverlay(
            navController = navController,
            drawerStatus = drawerStatus,
            debugDrawerDestinations = debugDrawerDestinations,
            onDrawerOpen = {
                debugDrawerStore.dispatch(DebugDrawerAction.DrawerOpened)
            },
            onDrawerClose = {
                debugDrawerStore.dispatch(DebugDrawerAction.DrawerClosed)
            },
            onDrawerBackButtonClick = {
                debugDrawerStore.dispatch(DebugDrawerAction.OnBackPressed)
            },
        )
    }
}

@PreviewLightDark
@Composable
private fun FenixOverlayPreview() {
    val selectedTab = createTab("https://mozilla.org")
    FenixOverlay(
        browserStore = BrowserStore(
            BrowserState(selectedTabId = selectedTab.id, tabs = listOf(selectedTab)),
        ),
        cfrToolsStore = CfrToolsStore(),
        gleanDebugToolsStore = GleanDebugToolsStore(
            initialState = GleanDebugToolsState(
                logPingsToConsoleEnabled = false,
                debugViewTag = "",
                pingTypes = listOf(
                    "metrics",
                    "baseline",
                    "ping type 3",
                    "ping type 4",
                ),
            ),
        ),
        inactiveTabsEnabled = true,
        loginsStorage = FakeLoginsStorage(),
        addressesDebugRegionRepository = FakeAddressesDebugRegionRepository(),
        creditCardsAddressesStorage = FakeCreditCardsAddressesStorage(),
        integrityClient = IntegrityClient.testSuccess,
    )
}
