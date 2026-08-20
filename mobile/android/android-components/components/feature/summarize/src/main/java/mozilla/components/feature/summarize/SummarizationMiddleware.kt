/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.feature.summarize.content.ContentProvider
import mozilla.components.feature.summarize.ext.fetchLlm
import mozilla.components.feature.summarize.ext.mapToRichDocument
import mozilla.components.feature.summarize.ext.prompt
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

const val TAG = "SummarizationMiddleware"

/**
 * The initial middleware for the summarization feature.
 *
 * @property llmProvider The cloud provider used to source a summarization [Llm]. A token is renewed by preparing the
 *   provider when it does not already hold a usable one.
 */
class SummarizationMiddleware(
    private val isPageLoadingFlow: Flow<Boolean>,
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
            is ViewAppeared ->
                scope.launch {
                    if (needsShakeConsent(store.state)) {
                        store.dispatch(ShakeConsentRequested)
                    } else if (isPageLoadingFlow.first()) {
                        store.dispatch(PageLoadStarted)
                        try {
                            withTimeout(PAGE_LOADING_TIMEOUT) {
                                isPageLoadingFlow.first { !it }
                                store.dispatch(PageLoadCompleted)
                            }
                        } catch (e: TimeoutCancellationException) {
                            store.dispatch(SummarizationFailed(e))
                        }
                    } else {
                        observeCloudLlmProvider(store)
                    }
                }
            OffDeviceSummarizationShakeConsentAction.CancelClicked ->
                scope.launch {
                    settings.incrementShakeConsentRejectedCount()
                }
            OffDeviceSummarizationShakeConsentAction.AllowClicked ->
                scope.launch {
                    settings.setHasConsentedToShake(true)
                    observeCloudLlmProvider(store)
                }
            is LlmProviderAction.ProviderInitialized ->
                scope.launch {
                    observePrompt(store, action.llm)
                }
            is SummarizationFailed ->
                scope.launch {
                    errorReporter.report(TAG, action.exception)
                }
            is PageLoadCompleted ->
                scope.launch {
                    observeCloudLlmProvider(store)
                }

            is ContentExtracted,
            DownloadConsentAction.AllowClicked,
            DownloadConsentAction.CancelClicked,
            DownloadConsentAction.LearnMoreClicked,
            DownloadErrorAction.CancelClicked,
            DownloadErrorAction.LearnMoreClicked,
            DownloadErrorAction.TryAgainClicked,
            DownloadInProgressAction.CancelClicked,
            ErrorAction.ErrorDismissed,
            ErrorAction.LearnMoreClicked,
            OffDeviceSummarizationShakeConsentAction.LearnMoreClicked,
            OnDeviceSummarizationShakeConsentAction.AllowClicked,
            OnDeviceSummarizationShakeConsentAction.CancelClicked,
            OnDeviceSummarizationShakeConsentAction.LearnMoreClicked,
            is LlmProviderAction.SignInRequired,
            PageLoadStarted,
            is ReceivedParsedDocument,
            SettingsBackClicked,
            SettingsClicked,
            is SettingsLoaded,
            ShakeConsentRequested,
            SignInSummarizationContentAction.DismissClicked,
            SignInSummarizationContentAction.LearnMoreClicked,
            SignInSummarizationContentAction.SignInClicked,
            SummarizationCompleted,
            is SummarizationRequested,
            is SummarizeSettingsActionWrapper,
            is ViewDismissed -> Unit
        }

        next(action)
    }

    @Suppress("TooGenericExceptionCaught")
    private suspend fun observePrompt(store: SummarizationStore, llm: Llm) {
        try {
            withTimeout(SUMMARIZE_TIMEOUT) {
                val content = contentProvider.getContent().getOrThrow()

                store.dispatch(ContentExtracted(content))

                llm.prompt(content.prompt)
                    .mapToRichDocument(
                        pageTitle = content.metadata.pageTitle,
                        dispatcher = dispatcher,
                    )
                    .onCompletion { if (it == null) store.dispatch(SummarizationCompleted) }
                    .collect { store.dispatch(ReceivedParsedDocument(it)) }
            }
        } catch (e: TimeoutCancellationException) {
            store.dispatch(SummarizationFailed(e))
        } catch (e: CancellationException) {
            throw e
        } catch (e: Throwable) {
            store.dispatch(SummarizationFailed(e))
        }
    }

    private suspend fun observeCloudLlmProvider(store: SummarizationStore) {
        if (llmProvider.state.value !is CloudLlmProvider.State.Ready) {
            llmProvider.prepare()
        }
        llmProvider.fetchLlm.collect { store.dispatch(it) }
    }

    private suspend fun needsShakeConsent(state: SummarizationState): Boolean =
        state is SummarizationState.Inert && state.initializedWithShake && !settings.getHasConsentedToShake().first()

    private companion object {
        val SUMMARIZE_TIMEOUT = 60.seconds
        val PAGE_LOADING_TIMEOUT = 60.seconds
    }
}
