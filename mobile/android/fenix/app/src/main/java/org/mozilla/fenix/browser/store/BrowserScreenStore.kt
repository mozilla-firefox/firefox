/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.UiStore
import org.mozilla.fenix.browser.store.BrowserScreenAction.CancelPrivateDownloadsOnPrivateTabsClosedAccepted
import org.mozilla.fenix.browser.store.BrowserScreenAction.ClosingLastPrivateTab

/**
 * [UiStore] for the browser screen.
 *
 * @param initialState The initial state of the store.
 * @param middleware The middlewares to be applied to this store.
 */
class BrowserScreenStore(
    initialState: BrowserScreenState = BrowserScreenState(),
    middleware: List<Middleware<BrowserScreenState, BrowserScreenAction>> = emptyList(),
) : UiStore<BrowserScreenState, BrowserScreenAction>(
    initialState = initialState,
    reducer = ::reduce,
    middleware = middleware,
)

private fun reduce(state: BrowserScreenState, action: BrowserScreenAction): BrowserScreenState = when (action) {
    is ClosingLastPrivateTab -> state.copy(
        cancelPrivateDownloadsAccepted = false,
    )

    is CancelPrivateDownloadsOnPrivateTabsClosedAccepted -> state.copy(
        cancelPrivateDownloadsAccepted = true,
    )
}
