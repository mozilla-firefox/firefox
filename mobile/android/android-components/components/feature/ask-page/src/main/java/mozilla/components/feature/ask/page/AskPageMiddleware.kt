/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.launch
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.feature.ask.page.ext.mapToRichDocument
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

class AskPageMiddleware(
    private val llmProvider: CloudLlmProvider,
    private val scope: CoroutineScope,
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
) : Middleware<AskPageState, AskPageAction> {

    override fun invoke(
        store: Store<AskPageState, AskPageAction>,
        next: (AskPageAction) -> Unit,
        action: AskPageAction,
    ) {
        val prevState = store.state
        next(action)
        when (action) {
            is ViewAppeared -> scope.launch { observeProvider(store) }
            is LlmProviderAction.ProviderAvailable -> scope.launch { llmProvider.prepare() }
            is LlmProviderAction.ProviderReady -> {
                val wasWaiting = prevState is AskPageState.WaitingToSendMessage
                if (wasWaiting) {
                    val ready = store.state as? AskPageState.Ready ?: return
                    scope.launch { sendPrompt(store, ready.llm, ready.messages) }
                }
            }
            is UserMessageSubmitted -> {
                val ready = store.state as? AskPageState.Ready ?: return
                scope.launch { sendPrompt(store, ready.llm, ready.messages) }
            }
            else -> {}
        }
    }

    private suspend fun sendPrompt(
        store: Store<AskPageState, AskPageAction>,
        llm: Llm,
        messages: List<Llm.Message>,
    ) {
        runCatching {
            llm.prompt(Llm.ContextWindow(messages))
                .mapToRichDocument(dispatcher)
                .onCompletion { if (it == null) store.dispatch(ResponseCompleted) }
                .collect { (markdown, document) ->
                    store.dispatch(ReceivedParsedResponse(PartialResponse(markdown, document)))
                }
        }.onFailure { store.dispatch(ResponseFailed(it)) }
    }

    private suspend fun observeProvider(store: Store<AskPageState, AskPageAction>) {
        llmProvider.state.collect { providerState ->
            store.dispatch(
                when (providerState) {
                    CloudLlmProvider.State.Available -> LlmProviderAction.ProviderAvailable
                    is CloudLlmProvider.State.Ready -> LlmProviderAction.ProviderReady(providerState.llm)
                    is CloudLlmProvider.State.Unavailable -> LlmProviderAction.ProviderFailed(providerState.exception)
                },
            )
        }
    }
}
