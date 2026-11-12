/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** [Middleware] implementation for managing [PromptRequest]s. */
class PromptMiddleware(private val scope: CoroutineScope) : Middleware<BrowserState, BrowserAction> {

    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        when (action) {
            is ContentAction.UpdatePromptRequestAction -> {
                if (shouldBlockPrompt(action, store)) {
                    return
                }
            }
            else -> {
                // no-op
            }
        }

        next(action)
    }

    private fun shouldBlockPrompt(
        action: ContentAction.UpdatePromptRequestAction,
        store: Store<BrowserState, BrowserAction>,
    ): Boolean {
        if (action.promptRequest is PromptRequest.Popup) {
            store.state.findTab(action.sessionId)?.let {
                if (it.content.promptRequests.lastOrNull { prompt -> prompt is PromptRequest.Popup } != null) {
                    scope.launch {
                        (action.promptRequest as PromptRequest.Popup).onDeny()
                    }
                    return true
                }
            }
        }
        return false
    }
}
