/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.launch
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.feature.summarize.content.ContentProvider
import mozilla.components.feature.summarize.ext.fetchLlm
import mozilla.components.feature.summarize.ext.mapToRichDocument
import mozilla.components.feature.summarize.ext.prompt
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** The initial middleware for the summarization feature */
class SummarizationMiddleware(
    private val settings: SummarizationSettings,
    private val llmProvider: CloudLlmProvider,
    private val contentProvider: ContentProvider,
    private val errorReporter: ErrorReporter,
    private val scope: CoroutineScope,
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
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
            OffDeviceSummarizationShakeConsentAction.CancelClicked -> scope.launch {
                settings.incrementShakeConsentRejectedCount()
            }
            OffDeviceSummarizationShakeConsentAction.AllowClicked -> scope.launch {
                settings.setHasConsentedToShake(true)
                observeCloudLlmProvider(store, llmProvider)
            }
            LlmProviderAction.ProviderAvailable -> scope.launch {
                llmProvider.prepare()
            }
            is LlmProviderAction.ProviderInitialized -> scope.launch {
                observePrompt(store, action.llm)
            }
            is SummarizationFailed -> scope.launch {
                errorReporter.report(action.throwable)
            }
        }

        next(action)
    }

    private suspend fun observePrompt(store: SummarizationStore, llm: Llm) = runCatching {
        val content = contentProvider.getContent().getOrThrow()

        store.dispatch(ContentExtracted(content))

        llm.prompt(content.prompt)
            .mapToRichDocument(dispatcher)
            .onCompletion { if (it == null) store.dispatch(SummarizationCompleted) }
            .collect { store.dispatch(ReceivedParsedDocument(it)) }
    }.onFailure { store.dispatch(SummarizationFailed(it)) }

    private suspend fun observeCloudLlmProvider(
        store: SummarizationStore,
        llmProvider: CloudLlmProvider,
    ) = llmProvider.fetchLlm.collect { store.dispatch(it) }

    private suspend fun needsShakeConsent(state: SummarizationState): Boolean =
        state is SummarizationState.Inert &&
            state.initializedWithShake &&
            !settings.getHasConsentedToShake().first()
}
