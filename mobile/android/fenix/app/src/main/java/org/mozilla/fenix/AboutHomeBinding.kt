/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.home.HomeFragment

/**
 * A binding for observing [ContentState.url] and navigating to the [HomeFragment] if the current session's url is
 * updated to [ABOUT_HOME_URL].
 */
class AboutHomeBinding(
    browserStore: BrowserStore,
    private val navController: NavController,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    private var lastSelectedTabUrl: String? = null

    override suspend fun onState(flow: Flow<BrowserState>) {
        flow
            .map { it.selectedTab?.content?.url }
            .distinctUntilChanged()
            .collect { url ->
                val isUrlChanged = url != lastSelectedTabUrl
                lastSelectedTabUrl = url
                if (
                    isUrlChanged &&
                        url == ABOUT_HOME_URL &&
                        listOf(
                                R.id.browserFragment,
                                R.id.menuFragment,
                                R.id.menuDialogFragment,
                            )
                            .contains(navController.currentDestination?.id)
                ) {
                    navController.navigate(NavGraphDirections.actionGlobalHome())
                }
            }
    }
}
