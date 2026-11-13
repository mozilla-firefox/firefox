/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.PictureInPictureSession
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * This middleware is for use with managing any states or resources required for picture in picture.
 *
 * @param releaseUseCase Provides the use case releasing the resources held for picture-in-picture. Only called when an
 *   action is processed, so it can depend on the store this middleware is part of.
 */
class PictureInPictureMiddleware(private val releaseUseCase: () -> PictureInPictureUseCases.ReleaseUseCase) :
    Middleware<BrowserState, BrowserAction> {
    private val release by lazy { releaseUseCase() }

    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        when (action) {
            is PictureInPictureAction.EnterAction ->
                if (store.state.canShowPictureInPicture(action.tabId)) {
                    // Replace the existing session by first closing the previous one.
                    store.state.pictureInPictureWindow.session?.let { release(it, keepTabId = action.tabId) }
                    next(action)
                } else {
                    release(PictureInPictureSession(action.tabId, action.engineSession))
                }
            is PictureInPictureAction.ExitAction -> {
                store.state.pictureInPictureWindow.session
                    ?.takeIf { it.tabId == action.tabId }
                    ?.let { release(it, keepTabId = action.tabId) }
                next(action)
            }
            is PictureInPictureAction.CloseWindowAction -> {
                store.state.pictureInPictureWindow.session?.let { release(it) }
                next(action)
            }
            else -> next(action)
        }
    }

    private fun BrowserState.canShowPictureInPicture(tabId: String) =
        pictureInPictureWindow.requestedTabId == tabId && findTabOrCustomTab(tabId) != null
}
