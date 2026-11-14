/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.PictureInPictureSession
import mozilla.components.browser.state.state.PictureInPictureWindowState

internal object PictureInPictureStateReducer {
    fun reduce(state: BrowserState, action: PictureInPictureAction): BrowserState =
        when (action) {
            is PictureInPictureAction.RequestAction ->
                state.copyWithPictureInPictureWindow { window -> window.copy(requestedTabId = action.tabId) }
            is PictureInPictureAction.ConsumeRequestAction ->
                state.copyWithPictureInPictureWindow { window ->
                    window.copy(requestedTabId = window.requestedTabId.takeUnless { it == action.tabId })
                }
            is PictureInPictureAction.EnterAction ->
                state.copyWithPictureInPictureWindow { window ->
                    window.copy(session = PictureInPictureSession(action.tabId, action.engineSession))
                }
            is PictureInPictureAction.ExitAction ->
                state.copyWithPictureInPictureWindow { window ->
                    window.copy(session = window.session?.takeUnless { it.tabId == action.tabId })
                }
            is PictureInPictureAction.CloseWindowAction ->
                state.copyWithPictureInPictureWindow { PictureInPictureWindowState() }
        }

    private inline fun BrowserState.copyWithPictureInPictureWindow(
        crossinline update: (PictureInPictureWindowState) -> PictureInPictureWindowState
    ): BrowserState = copy(pictureInPictureWindow = update(pictureInPictureWindow))
}
