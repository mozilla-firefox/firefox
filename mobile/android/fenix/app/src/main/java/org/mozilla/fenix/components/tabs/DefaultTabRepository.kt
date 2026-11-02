/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.tabs

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.isActive
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.repository.TabRepository
import mozilla.components.feature.tabs.TabsUseCases
import org.mozilla.fenix.ext.maxActiveTime
import org.mozilla.fenix.tabstray.ext.toDisplayTitle

/**
 * Default implementation of [TabRepository], backed by the [BrowserStore].
 *
 * @param browserStore The [BrowserStore] holding the open tabs to observe.
 * @param tabsUseCases The [TabsUseCases] used to update the open tabs.
 */
class DefaultTabRepository(
    browserStore: BrowserStore,
    private val tabsUseCases: TabsUseCases,
) : TabRepository {

    override val tabFlow: Flow<TabRepository.Data> =
        browserStore.stateFlow
            .map { state ->
                TabRepository.Data(
                    selectedTabId = state.selectedTabId,
                    tabs = state.tabs.map { it.toTab() },
                )
            }
            .distinctUntilChanged()

    override fun moveTabs(tabIds: List<String>, targetTabId: String, placeAfter: Boolean) {
        tabsUseCases.moveTabs.invoke(tabIds = tabIds, targetTabId = targetTabId, placeAfter = placeAfter)
    }

    override fun moveTab(sourceTabId: String, targetTabId: String, placeAfter: Boolean) {
        tabsUseCases.moveTabs.invoke(sourceTabId = sourceTabId, targetTabId = targetTabId, placeAfter = placeAfter)
    }

    override fun removeTabs(tabIds: List<String>, excludedFallbackTabIds: Set<String>) {
        tabsUseCases.removeTabs.invoke(ids = tabIds, excludedFallbackTabIds = excludedFallbackTabIds)
    }

    override fun selectTab(tabId: String) {
        tabsUseCases.selectTab.invoke(tabId)
    }
}

private fun TabSessionState.toTab(): Tab =
    Tab(
        id = id,
        url = content.url,
        title = toDisplayTitle(),
        private = content.private,
        inactive = !isActive(maxActiveTime = maxActiveTime),
        icon = content.icon,
        isMediaActive = mediaSessionState?.playbackState == MediaSession.PlaybackState.PLAYING,
        lastAccess = lastAccess,
    )
