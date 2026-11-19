/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recenttabs

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.ext.asRecentTabs

/**
 * View-bound feature that dispatches recent tab changes to the [AppStore] when the [BrowserStore] is updated.
 *
 * @param browserStore The [BrowserStore] to observe the tabs from.
 * @param appStore The [AppStore] the recent tab changes are dispatched to.
 * @param tabDataCoordinator The [TabDataCoordinator] used to exclude the tabs in closed tab groups. No recent tab
 *   changes are dispatched until it is initialized.
 * @param mainDispatcher The [CoroutineDispatcher] on which the [BrowserStore] is observed.
 */
class RecentTabsListFeature(
    browserStore: BrowserStore,
    private val appStore: AppStore,
    private val tabDataCoordinator: TabDataCoordinator,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<BrowserState>) {
        // Listen for changes regarding the currently selected tab and in progress media tab.
        flow
            .combine(tabDataCoordinator.closedTabGroupTabIds()) { state, excludedTabIds ->
                state.asRecentTabs(excludedTabIds = excludedTabIds)
            }
            .distinctUntilChanged()
            .collect {
                appStore.dispatch(AppAction.RecentTabsChange(it))
            }
    }
}

sealed class RecentTab {
    /**
     * A tab that was recently viewed
     *
     * @property state Recently viewed [TabSessionState]
     */
    data class Tab(val state: TabSessionState) : RecentTab()
}

/**
 * Returns a [Flow] of the IDs of the tabs in closed tab groups, which only emits once the coordinator is initialized.
 */
private fun TabDataCoordinator.closedTabGroupTabIds(): Flow<Set<String>> =
    tabsSnapshot
        .filterNotNull()
        .map { snapshot -> snapshot.tabGroups.filter { it.closed }.flatMap { it.tabIds }.toSet() }
        .distinctUntilChanged()
