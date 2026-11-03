/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.home.HomeFragment

/**
 * A binding for observing [ContentState.url] and navigating to the [HomeFragment] if the current session's url is
 * updated to [ABOUT_HOME_URL] and it was previously another URL. If the url is updated while another screen is shown,
 * the navigation is deferred until the user returns to the browser.
 */
class AboutHomeBinding(
    private val browserStore: BrowserStore,
    private val navController: NavController,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    private var hasPendingHomepageNavigation = false

    /**
     * Cache the last URL in the binding to have a memory of it in between the application being put to background and
     * then resumed back which would start a new tab.url observer with no memory of the previous url.
     */
    private var lastObservedUrl: String? = null

    override suspend fun onState(flow: Flow<BrowserState>) {
        coroutineScope {
            launch { observeSelectedTabUrl(flow) }
            launch { observeDestinationChanges() }
        }
    }

    private suspend fun observeSelectedTabUrl(flow: Flow<BrowserState>) {
        flow
            .map { it.selectedTab?.content?.url }
            .collect { url ->
                if (url == lastObservedUrl) {
                    return@collect
                }
                lastObservedUrl = url

                val currentDestinationId = navController.currentDestination?.id
                hasPendingHomepageNavigation = false

                if (url != ABOUT_HOME_URL) {
                    return@collect
                }

                when (currentDestinationId) {
                    R.id.browserFragment,
                    R.id.menuFragment,
                    R.id.menuDialogFragment -> navController.navigate(NavGraphDirections.actionGlobalHome())
                    R.id.homeFragment -> Unit
                    else -> hasPendingHomepageNavigation = true
                }
            }
    }

    private suspend fun observeDestinationChanges() {
        navController.currentBackStackEntryFlow
            .map { it.destination.id }
            .collect { destinationId ->
                when (destinationId) {
                    R.id.homeFragment -> hasPendingHomepageNavigation = false
                    R.id.browserFragment -> {
                        if (
                            hasPendingHomepageNavigation &&
                                browserStore.state.selectedTab?.content?.url == ABOUT_HOME_URL
                        ) {
                            hasPendingHomepageNavigation = false
                            navController.navigate(NavGraphDirections.actionGlobalHome())
                        }
                    }
                }
            }
    }
}
