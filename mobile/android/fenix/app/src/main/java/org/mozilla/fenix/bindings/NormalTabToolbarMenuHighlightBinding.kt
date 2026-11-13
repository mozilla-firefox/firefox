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
import org.mozilla.fenix.tabstray.ext.isNormalTab

/**
 * Binding between [AppStore] and [BrowserStore] that adds [notification] to the toolbar menu while [highlight] emits
 * true and a normal tab is selected, and removes it otherwise.
 *
 * @param appStore [AppStore] to add and remove the notification in.
 * @param notification The [SupportedMenuNotifications] this binding is responsible for.
 * @param highlight Emits whether the feature currently wants the toolbar menu button highlighted.
 * @param browserStore [BrowserStore] to observe the selected tab in.
 * @param mainDispatcher [CoroutineDispatcher] the store is observed on.
 */
class NormalTabToolbarMenuHighlightBinding(
    private val appStore: AppStore,
    private val notification: SupportedMenuNotifications,
    private val highlight: Flow<Boolean>,
    browserStore: BrowserStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<BrowserState>) {
        combine(
                highlight,
                flow.filter { it.selectedTab?.content?.url != null }.map { it.selectedTab?.isNormalTab() == true },
            ) { highlightToolbar, isNormalTab ->
                // we only want to highlight the toolbar for normal tabs
                highlightToolbar && isNormalTab
            }
            .distinctUntilChanged()
            .collect { highlight ->
                val action =
                    if (highlight) {
                        AppAction.MenuNotification.AddMenuNotification(notification)
                    } else {
                        AppAction.MenuNotification.RemoveMenuNotification(notification)
                    }
                appStore.dispatch(action = action)
            }
    }
}
