/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.browsingmode

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.helpers.AbstractBinding

/**
 * Observes the selected tab of the [store] so that the browsing mode can follow the selected tab.
 *
 * @param store The [BrowserStore] to observe.
 * @param customTabSessionId The ID of the custom tab session being displayed, if any.
 * @param onSelectedTabChanged Invoked with the newly selected tab.
 * @param mainDispatcher The [CoroutineDispatcher] used to observe the [store].
 */
class BrowsingModeObserver(
    store: BrowserStore,
    private val customTabSessionId: String?,
    private val onSelectedTabChanged: (TabSessionState) -> Unit,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(store, mainDispatcher) {
    override suspend fun onState(flow: Flow<BrowserState>) {
        flow
            .mapNotNull { it.selectedTab }
            .distinctUntilChangedBy { it.id }
            .collect { selectedTab ->
                if (customTabSessionId == null) {
                    onSelectedTabChanged(selectedTab)
                }
            }
    }
}
