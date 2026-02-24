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
            is ViewAppeared -> checkForShakeConsent(store.state) { requiresConsent ->
                if (requiresConsent) {
                    store.dispatch(ShakeConsentRequested)
                } else {
                    store.dispatch(LlmAction.Initialize)
                }
            }
            OffDeviceSummarizationShakeConsentAction.AllowClicked -> scope.launch {
                settings.setHasConsentedToShake(true)
                store.dispatch(LlmAction.Initialize)
            }
            LlmAction.Initialize -> observeCloudLlmProvider(store, llmProvider)
            LlmProviderAction.ProviderNotReady -> scope.launch {
                llmProvider.prepare()
            }
            is LlmProviderAction.ProviderReady -> observePrompt(store, action.llm)
        }

        next(action)
    }

    private fun observePrompt(store: SummarizationStore, llm: Llm) = scope.launch {
        store.dispatch(LlmAction.SummarizationRequested)
        llm.prompt(Prompt(systemPrompt))
            .collect { response ->
                store.dispatch(LlmAction.ReceivedResponse(response))
            }
    }

    private fun observeCloudLlmProvider(
        store: SummarizationStore,
        llmProvider: CloudLlmProvider,
    ) = scope.launch {
        llmProvider.state.map { state ->
            when (state) {
                CloudLlmProvider.State.Available -> LlmProviderAction.ProviderNotReady
                CloudLlmProvider.State.Unavailable -> LlmProviderAction.ProviderError
                is CloudLlmProvider.State.Ready -> LlmProviderAction.ProviderReady(state.llm)
            }
        }.collect { store.dispatch(it) }
    }

    private fun checkForShakeConsent(
        state: SummarizationState,
        requiresShakeConsent: (Boolean) -> Unit,
    ) = scope.launch {
        requiresShakeConsent(
            state is SummarizationState.Inert &&
                state.initializedWithShake &&
                !settings.getHasConsentedToShake(),
        )
    }

    private val systemPrompt = """
        This is the system prompt: 
    """.trimIndent()
}
