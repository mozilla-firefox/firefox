/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** The initial middleware for the summarization feature */
class SummarizationMiddleware(
    private val settings: SummarizationSettings,
    private val llmProvider: CloudLlmProvider,
    private val scope: CoroutineScope,
) : Middleware<SummarizationState, SummarizationAction> {
    override fun invoke(
        store: Store<SummarizationState, SummarizationAction>,
        next: (SummarizationAction) -> Unit,
        action: SummarizationAction,
    ) {
        when (action) {
            is ViewAppeared -> scope.launch {
                if (needsShakeConsent(store.state)) {
                    store.dispatch(ShakeConsentRequested)
                } else {
                    observeCloudLlmProvider(store, llmProvider)
                }
            }
            OffDeviceSummarizationShakeConsentAction.AllowClicked -> scope.launch {
                settings.setHasConsentedToShake(true)
                observeCloudLlmProvider(store, llmProvider)
            }
            LlmProviderAction.ProviderUnavailable -> scope.launch {
                llmProvider.prepare()
            }
            is LlmProviderAction.ProviderInitialized -> scope.launch {
                observePrompt(store, action.llm)
            }
        }

        next(action)
    }

    private suspend fun observePrompt(store: SummarizationStore, llm: Llm) {
        store.dispatch(LlmAction.SummarizationRequested)
        llm.prompt(Prompt(systemPrompt))
            .collect { response ->
                store.dispatch(LlmAction.ReceivedResponse(response))
            }
    }

    private suspend fun observeCloudLlmProvider(
        store: SummarizationStore,
        llmProvider: CloudLlmProvider,
    ) {
        llmProvider.state.map { state ->
            when (state) {
                CloudLlmProvider.State.Available -> LlmProviderAction.ProviderUnavailable
                CloudLlmProvider.State.Unavailable -> LlmProviderAction.ProviderFailed
                is CloudLlmProvider.State.Ready -> LlmProviderAction.ProviderInitialized(state.llm)
            }
        }.collect { store.dispatch(it) }
    }

    private suspend fun needsShakeConsent(state: SummarizationState): Boolean =
        state is SummarizationState.Inert &&
            state.initializedWithShake &&
            !settings.getHasConsentedToShake()

    private val systemPrompt = "This is the system prompt: "
}
