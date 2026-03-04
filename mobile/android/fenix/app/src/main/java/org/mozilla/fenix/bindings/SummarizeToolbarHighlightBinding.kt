/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bindings

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.summarization.onboarding.SummarizationFeatureDiscoveryConfiguration
import org.mozilla.fenix.tabstray.ext.isNormalTab

/**
 * Binding between [AppStore] and [BrowserStore] that determines if and when to add a
 * toolbar notification for the "Summarize" feature
 */
class SummarizeToolbarHighlightBinding(
    private val appStore: AppStore,
    private val featureDiscoverySettings: SummarizationFeatureDiscoveryConfiguration,
    browserStore: BrowserStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<BrowserState>) {
        combine(
            featureDiscoverySettings.toolbarMenuButtonHighlight,
            flow.filter { it.selectedTab?.content?.url != null }
                .map { it.selectedTab?.isNormalTab() == true },
        ) { highlightToolbar, isNormalTab ->
            // we only want to highlight the toolbar for normal tabs
            highlightToolbar && isNormalTab
        }.distinctUntilChanged()
            .collect { highlight ->
                val action = if (highlight) {
                    AppAction.MenuNotification.AddMenuNotification(SupportedMenuNotifications.Summarize)
                } else {
                    AppAction.MenuNotification.RemoveMenuNotification(SupportedMenuNotifications.Summarize)
                }
                appStore.dispatch(action = action)
            }
    }
}
