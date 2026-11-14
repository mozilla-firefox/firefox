/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.PictureInPictureSession
import mozilla.components.lib.state.Store

/**
 * Contains use cases for opening, closing and releasing the picture-in-picture window.
 *
 * @param store The store the picture-in-picture window is managed through.
 * @param scope Scope the engine is called in.
 */
class PictureInPictureUseCases(
    store: Store<BrowserState, BrowserAction>,
    scope: CoroutineScope = MainScope(),
) {
    /** Use case requesting a picture-in-picture window for the video of a tab. */
    class RequestUseCase internal constructor(private val store: Store<BrowserState, BrowserAction>) {
        /** Requests a picture-in-picture window for the video of the tab with ID [tabId]. */
        operator fun invoke(tabId: String? = store.state.selectedTabId) {
            if (tabId == null) {
                return
            }

            store.dispatch(PictureInPictureAction.RequestAction(tabId))
        }
    }

    /** Use case closing the picture-in-picture window. */
    class CloseWindowUseCase internal constructor(private val store: Store<BrowserState, BrowserAction>) {
        /** Closes the picture-in-picture window, releasing the session it shows. */
        operator fun invoke() {
            store.dispatch(PictureInPictureAction.CloseWindowAction)
        }
    }

    /** Use case releasing the resources held for showing the video of a tab in a picture-in-picture window. */
    class ReleaseUseCase
    internal constructor(
        private val store: Store<BrowserState, BrowserAction>,
        private val scope: CoroutineScope,
    ) {
        /**
         * Closes the engine session of [session] and, unless its tab is [keepTabId], tells the engine the video of that
         * tab is no longer shown in picture-in-picture.
         */
        operator fun invoke(session: PictureInPictureSession, keepTabId: String? = null) {
            scope.launch {
                if (session.tabId != keepTabId) {
                    store.state.findTabOrCustomTab(session.tabId)?.engineState?.engineSession?.exitPictureInPicture()
                }
                session.engineSession.close()
            }
        }
    }

    val request: RequestUseCase by lazy { RequestUseCase(store) }
    val closeWindow: CloseWindowUseCase by lazy { CloseWindowUseCase(store) }
    val release: ReleaseUseCase by lazy { ReleaseUseCase(store, scope) }
}
