/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bindings

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.selector.pendingExtensionUpdatesNeedingAttention
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications

/**
 * Shows the menu button dot ([SupportedMenuNotifications.ExtensionUpdate]) while an enabled extension has an update
 * waiting for review.
 *
 * @param appStore The application store for dispatching actions.
 * @param browserStore The browser store to observe state changes.
 * @param mainDispatcher The [CoroutineDispatcher] on which the state observation and updates will occur.
 */
class PendingExtensionUpdateBinding(
    private val appStore: AppStore,
    browserStore: BrowserStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<BrowserState>) {
        flow
            .map { it.pendingExtensionUpdatesNeedingAttention.isNotEmpty() }
            .distinctUntilChanged()
            .collect { hasPendingUpdates ->
                val action =
                    if (hasPendingUpdates) {
                        AppAction.MenuNotification.AddMenuNotification(SupportedMenuNotifications.ExtensionUpdate)
                    } else {
                        AppAction.MenuNotification.RemoveMenuNotification(SupportedMenuNotifications.ExtensionUpdate)
                    }
                appStore.dispatch(action)
            }
    }
}
