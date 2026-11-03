/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.action.ListenToPageAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlaybackPhase
import mozilla.components.lib.state.helpers.AbstractBinding

/**
 * Binding to update the [BrowserStore] by listening to changes to [ListenState].
 *
 * @param listenStore [ListenStore] used to listen for changes to [ListenState].
 * @param browserStore [BrowserStore] to update with the tab ID that is being read out loud.
 * @param mainDispatcher The [CoroutineDispatcher] on which the state observation and updates will occur. Defaults to
 *   [Dispatchers.Main].
 */
class ListenToPageBinding(
    listenStore: ListenStore,
    private val browserStore: BrowserStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<ListenState>(listenStore, mainDispatcher) {
    override suspend fun onState(flow: Flow<ListenState>) {
        var previousTabId: String? = null

        flow
            .map {
                ListenTabStatus(
                    tabId = it.tabId,
                    showMediaIcon = shouldShowMediaIcon(it.playbackState.phase, it.error),
                )
            }
            .distinctUntilChanged()
            .collect { (tabId, showMediaIcon) ->
                previousTabId
                    ?.takeIf { it != tabId }
                    ?.let {
                        browserStore.dispatch(ListenToPageAction.ListenToPageDeactivated(it))
                    }
                if (tabId != null) {
                    browserStore.dispatch(
                        if (showMediaIcon) {
                            ListenToPageAction.ListenToPageActivated(tabId)
                        } else {
                            ListenToPageAction.ListenToPageDeactivated(tabId)
                        }
                    )
                }
                previousTabId = tabId
            }
    }

    private fun shouldShowMediaIcon(phase: PlaybackPhase, error: ListenError?): Boolean {
        return (phase == PlaybackPhase.Playing || phase == PlaybackPhase.Buffering) && error == null
    }
}

private data class ListenTabStatus(val tabId: String?, val showMediaIcon: Boolean)
