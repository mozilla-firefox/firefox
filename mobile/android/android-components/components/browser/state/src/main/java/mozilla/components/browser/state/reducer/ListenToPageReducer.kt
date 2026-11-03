/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.ListenToPageAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState

internal object ListenToPageReducer {
    /** [ListenToPageAction] Reducer function for modifying [TabSessionState.listenToPageActive] state. */
    fun reduce(state: BrowserState, action: ListenToPageAction): BrowserState =
        state.updateTabState(action.tabId) { sessionState ->
            sessionState.copy(listenToPageActive = action is ListenToPageAction.ListenToPageActivated)
        }
}
