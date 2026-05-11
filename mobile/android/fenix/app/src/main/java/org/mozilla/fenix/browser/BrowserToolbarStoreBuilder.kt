/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.coroutineScope
import androidx.navigation.NavController
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.DisplayState
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.readermode.ReaderModeController
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.toolbar.BrowserToolbarMiddleware
import org.mozilla.fenix.components.toolbar.BrowserToolbarTelemetryMiddleware
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isTallWindow
import org.mozilla.fenix.ext.isWideWindow
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchStatusSyncMiddleware
import org.mozilla.fenix.utils.Settings

/**
 * Delegate for building the [BrowserToolbarStore] used in the browser screen.
 */
object BrowserToolbarStoreBuilder {

    /**
     * Build the [BrowserToolbarStore] used in the browser screen.
     *
     * @param activity [AppCompatActivity] hosting the toolbar.
     * @param fragment [Fragment] as a [LifecycleOwner] to used to organize lifecycle dependent operations.
     * @param navController [NavController] to use for navigating to other in-app destinations.
     * @param appStore [AppStore] to sync from.
     * @param browserScreenStore [BrowserScreenStore] used for integration with other browser screen functionalities.
     * @param browserStore [BrowserStore] used for observing the browsing details.
     * @param components [Components] allowing interactions with other application features.
     * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
     * @param thumbnailsFeature [BrowserThumbnails] for requesting screenshots of the current tab.
     * @param readerModeController [ReaderModeController] for managing the reader mode.
     * @param settings [Settings] object to get the toolbar position and other settings.
     * @param customTabSession [CustomTabSessionState] if the toolbar is shown in a custom tab.
     * @param isSandboxCustomTab Whether the custom tab is sandboxed.
     */
    @Suppress("LongParameterList", "LongMethod")
    fun build(
        activity: AppCompatActivity,
        fragment: Fragment,
        navController: NavController,
        appStore: AppStore,
        browserScreenStore: BrowserScreenStore,
        browserStore: BrowserStore,
        components: Components,
        browsingModeManager: BrowsingModeManager,
        thumbnailsFeature: () -> BrowserThumbnails?,
        readerModeController: ReaderModeController,
        settings: Settings,
        customTabSession: CustomTabSessionState? = null,
        isSandboxCustomTab: Boolean = false,
    ) = fragment.fragmentStore(
        BrowserToolbarState(
            displayState = DisplayState(
                pageOrigin = PageOrigin(
                    hint = R.string.search_hint,
                    title = null,
                    url = null,
                    onClick = object : BrowserToolbarEvent {},
                ),
            ),
        ),
    ) {
        val lifecycleScope = fragment.viewLifecycleOwner.lifecycle.coroutineScope

        BrowserToolbarStore(
            initialState = it,
            middleware = when (customTabSession) {
                null -> listOf(
                    BrowserToolbarMiddleware(
                        uiContext = activity,
                        appStore = appStore,
                        browserScreenStore = browserScreenStore,
                        browserStore = browserStore,
                        ipProtectionStore = components.ipProtectionStore,
                        permissionsStorage = components.core.geckoSitePermissionsStorage,
                        cookieBannersStorage = components.core.cookieBannersStorage,
                        bookmarksStorage = activity.components.core.bookmarksStorage,
                        trackingProtectionUseCases = components.useCases.trackingProtectionUseCases,
                        useCases = components.useCases,
                        nimbusComponents = components.nimbus,
                        clipboard = activity.components.clipboardHandler,
                        publicSuffixList = components.publicSuffixList,
                        settings = settings,
                        navController = navController,
                        browsingModeManager = browsingModeManager,
                        readerModeController = readerModeController,
                        thumbnailsFeature = thumbnailsFeature,
                        isWideScreen = { fragment.isWideWindow() },
                        isTallScreen = { fragment.isTallWindow() },
                        scope = lifecycleScope,
                    ),
                    BrowserToolbarSearchStatusSyncMiddleware(
                        appStore = appStore,
                        browsingModeManager = browsingModeManager,
                        scope = lifecycleScope,
                    ),
                    BrowserToolbarSearchMiddleware(
                        uiContext = activity,
                        appStore = appStore,
                        browserStore = browserStore,
                        components = components,
                        navController = navController,
                        browsingModeManager = browsingModeManager,
                        settings = settings,
                        scope = lifecycleScope,
                    ),
                    BrowserToolbarTelemetryMiddleware(),
                )

                else -> listOf(
                    CustomTabBrowserToolbarMiddleware(
                        uiContext = activity,
                        requireNotNull(customTabSession).id,
                        browserStore = browserStore,
                        appStore = appStore,
                        ipProtectionStore = components.ipProtectionStore,
                        permissionsStorage = components.core.geckoSitePermissionsStorage,
                        cookieBannersStorage = components.core.cookieBannersStorage,
                        useCases = components.useCases.customTabsUseCases,
                        trackingProtectionUseCases = components.useCases.trackingProtectionUseCases,
                        publicSuffixList = components.publicSuffixList,
                        clipboard = activity.components.clipboardHandler,
                        navController = navController,
                        closeTabDelegate = { activity.finishAndRemoveTask() },
                        settings = settings,
                        scope = lifecycleScope,
                        isSandboxCustomTab = isSandboxCustomTab,
                    ),
                )
            },
        )
    }
}
