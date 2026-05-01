/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.launch
import mozilla.components.concept.llm.LlmSession
import mozilla.components.feature.ask.page.ext.mapToRichDocument
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

class AskPageMiddleware(
    private val session: LlmSession,
    private val scope: CoroutineScope,
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
) : Middleware<AskPageState, AskPageAction> {

    override fun invoke(
        store: Store<AskPageState, AskPageAction>,
        next: (AskPageAction) -> Unit,
        action: AskPageAction,
    ) {
        next(action)
        if (action is UserMessageSubmitted) {
            scope.launch { sendMessage(store, action.text) }
        }
    }

    private suspend fun sendMessage(store: Store<AskPageState, AskPageAction>, text: String) {
        runCatching {
            session.send(text)
                .mapToRichDocument(dispatcher)
                .onCompletion { if (it == null) store.dispatch(ResponseCompleted) }
                .collect { (markdown, document) ->
                    store.dispatch(ReceivedParsedResponse(PartialResponse(markdown, document)))
                }
        }.onFailure { store.dispatch(ResponseFailed(it)) }
    }
}
