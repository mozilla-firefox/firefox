/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowStore
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.toTabStorageUpdate
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

/**
 * Observes the [TabDataCoordinator] and maps its tab groups to [TabsTrayItem.TabGroup]s, using the same mapping as the
 * tabs tray, then dispatches them to the [TabGroupFlowStore] as [TabGroupAction.TabGroupsUpdated].
 *
 * @param tabDataCoordinator Used to listen for tab and group updates
 * @param store The [TabGroupFlowStore] the mapped tab groups are dispatched to
 * @param computationDispatcher The [CoroutineDispatcher] on which computations occur
 * @param mainDispatcher The [CoroutineDispatcher] on which observations occur
 */
internal class EnrichedTabGroupBinding(
    private val tabDataCoordinator: TabDataCoordinator,
    private val store: TabGroupFlowStore,
    private val computationDispatcher: CoroutineDispatcher = Dispatchers.Default,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {
    private var scope: CoroutineScope? = null

    override fun start() {
        scope = CoroutineScope(mainDispatcher).also { it.launch { observe() } }
    }

    override fun stop() {
        scope?.cancel()
        scope = null
    }

    private suspend fun observe() {
        tabDataCoordinator.tabsSnapshot
            .filterNotNull()
            .map { it.toTabStorageUpdate().tabGroups }
            .distinctUntilChanged()
            .flowOn(computationDispatcher)
            .collect { store.dispatch(TabGroupAction.TabGroupsUpdated(it)) }
    }
}
