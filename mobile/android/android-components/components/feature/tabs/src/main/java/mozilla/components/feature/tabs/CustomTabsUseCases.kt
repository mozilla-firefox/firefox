/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabs

import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabConfig
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.manifest.WebAppManifest
import mozilla.components.feature.session.SessionUseCases

/** UseCases for custom tabs. */
class CustomTabsUseCases(
    store: BrowserStore,
    loadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase,
) {
    /** Use case for adding a new custom tab. */
    class AddCustomTabUseCase(
        private val store: BrowserStore,
        private val loadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase,
    ) {
        /**
         * Adds a new custom tab with URL [url].
         *
         * @param url The URL to load in the custom tab.
         * @param customTabConfig The configuration of the custom tab.
         * @param private Whether the custom tab is created as a private tab.
         * @param additionalHeaders The extra headers to use when loading the URL.
         * @param source Where the request to open the custom tab came from.
         * @param desktopMode Whether the tab requests desktop sites, or `null` to follow [BrowserState.desktopMode].
         */
        operator fun invoke(
            url: String,
            customTabConfig: CustomTabConfig,
            private: Boolean = false,
            additionalHeaders: Map<String, String>? = null,
            source: SessionState.Source,
            desktopMode: Boolean? = null,
        ): String {
            val loadUrlFlags = EngineSession.LoadUrlFlags.external()
            val tab =
                createCustomTab(
                    url = url,
                    private = private,
                    source = source,
                    config = customTabConfig,
                    initialLoadFlags = loadUrlFlags,
                    desktopMode = desktopMode ?: store.state.desktopMode,
                )

            store.dispatch(CustomTabListAction.AddCustomTabAction(tab))
            loadUrlUseCase(url, tab.id, loadUrlFlags, additionalHeaders)
            return tab.id
        }
    }

    /** Use case for adding a new Web App tab. */
    class AddWebAppTabUseCase(
        private val store: BrowserStore,
        private val loadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase,
    ) {
        /** Adds a new web app tab with the given manifest. */
        operator fun invoke(
            url: String,
            source: SessionState.Source,
            customTabConfig: CustomTabConfig,
            webAppManifest: WebAppManifest,
        ): String {
            val loadUrlFlags = EngineSession.LoadUrlFlags.external()
            val tab =
                createCustomTab(
                    url = url,
                    source = source,
                    config = customTabConfig,
                    webAppManifest = webAppManifest,
                    initialLoadFlags = loadUrlFlags,
                )

            store.dispatch(CustomTabListAction.AddCustomTabAction(tab))
            loadUrlUseCase(url, tab.id, loadUrlFlags)
            return tab.id
        }
    }

    /** Use case for removing a custom tab. */
    class RemoveCustomTabUseCase(private val store: BrowserStore) {
        /** Removes the custom tab with the given [customTabId]. */
        operator fun invoke(customTabId: String): Boolean {
            val tab = store.state.findCustomTab(customTabId)
            if (tab != null) {
                store.dispatch(CustomTabListAction.RemoveCustomTabAction(tab.id))
                return true
            }
            return false
        }
    }

    /** Use case for migrating a custom tab to a regular tab. */
    class MigrateCustomTabUseCase(private val store: BrowserStore) {
        /**
         * Migrates the custom tab with the given [customTabId] to a regular tab. This method has no effect if the
         * custom tab does not exist.
         *
         * @param customTabId the custom tab to turn into a regular tab.
         * @param select whether or not to select the regular tab, defaults to true.
         */
        operator fun invoke(customTabId: String, select: Boolean = true) {
            store.dispatch(CustomTabListAction.TurnCustomTabIntoNormalTabAction(customTabId))

            if (select) {
                store.dispatch(TabListAction.SelectTabAction(customTabId))
            }
        }
    }

    val add by lazy { AddCustomTabUseCase(store, loadUrlUseCase) }
    val remove by lazy { RemoveCustomTabUseCase(store) }
    val migrate by lazy { MigrateCustomTabUseCase(store) }
    val addWebApp by lazy { AddWebAppTabUseCase(store, loadUrlUseCase) }
}
