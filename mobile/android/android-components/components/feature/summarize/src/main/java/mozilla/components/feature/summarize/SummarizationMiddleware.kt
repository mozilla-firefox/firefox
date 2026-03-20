/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.scan
import kotlinx.coroutines.flow.transform
import kotlinx.coroutines.launch
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.feature.summarize.content.PageContentExtractor
import mozilla.components.feature.summarize.content.PageMetadata
import mozilla.components.feature.summarize.content.PageMetadataExtractor
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.ui.richtext.parsing.Parser

/** The initial middleware for the summarization feature */
class SummarizationMiddleware(
    private val settings: SummarizationSettings,
    private val llmProvider: CloudLlmProvider,
    private val pageContentExtractor: PageContentExtractor,
    private val pageMetadataExtractor: PageMetadataExtractor,
    private val errorReporter: ErrorReporter,
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
            OffDeviceSummarizationShakeConsentAction.CancelClicked -> scope.launch {
                settings.incrementShakeConsentRejectedCount()
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
            is SummarizationFailed -> scope.launch {
                errorReporter.report(action.throwable)
            }
        }

        next(action)
    }

    private suspend fun observePrompt(store: SummarizationStore, llm: Llm) = runCatching {
        val pageMetadata = pageMetadataExtractor.getPageMetadata()
            .getOrDefault(PageMetadata(listOf(), "en"))

        val content = pageContentExtractor.getPageContent().getOrThrow()

        val parser = Parser()

        llm.prompt(Prompt("${pageMetadata.systemPrompt} $content"))
            // We want to accumulate and parse the values that we get from [ReplyPart]
            // So we emit them and dispatch any other value we get from the [Llm]
            .transform {
                when (it) {
                    is Llm.Response.Success.ReplyPart -> emit(it.value)
                    else -> store.dispatch(ReceivedLlmResponse(it))
                }
            }
            .scan("") { acc, i -> acc + i }
            .map { parser.parse(it) }
            .collect { store.dispatch(ReceivedParsedDocument(it)) }
    }.onFailure {
        store.dispatch(SummarizationFailed(it))
    }

    private suspend fun observeCloudLlmProvider(
        store: SummarizationStore,
        llmProvider: CloudLlmProvider,
    ) {
        store.dispatch(SummarizationRequested(llmProvider.info))
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
            !settings.getHasConsentedToShake().first()
}

private val PageMetadata.isRecipe get() = structuredDataTypes.any { it.lowercase() == "recipe" }
private val PageMetadata.systemPrompt get() = if (isRecipe) {
    recipeInstructions(language)
} else {
    defaultInstructions(language)
}

internal fun defaultInstructions(language: String) = """
        You are an expert at creating mobile-optimized summaries.
        You MUST respond entirely in $language. Do not mix languages.
        Process:
        Step 1: Identify the type of content.
        Step 2: Based on content type, prioritize:
        Recipe - Servings, Total time, Ingredients list, Key steps, Tips.
        News - What happened, when, where. How-to - Total time, Materials, Key steps, Warnings.
        Review - Bottom line rating, price. Opinion - Main arguments, Key evidence.
        Personal Blog - Author, main points. Fiction - Author, summary of plot.
        All other content types - Provide a brief summary of no more than 6 sentences.
        Step 3: Format for mobile using concise language and paragraphs with 3 sentences maximum.
        Bold critical details (numbers, warnings, key terms).
    """.trimIndent()

internal fun recipeInstructions(language: String) = """
        You are an expert at creating mobile-optimized recipe summaries.

        You MUST respond entirely in $language. Do not mix languages.
        Translate all visible section headers and labels into **{lang}**.
        Output ONLY the formatted result. Do not add any closing phrases.
        If a field is null, empty, or missing, omit that section entirely.
        Always replace placeholders with actual values.
        Convert time values to minutes and hours.

        FORMAT:
        **Servings:** {servings}

        **Total Time:** {total_time}

        **Prep Time:** {prep_time}

        **Cook Time:** {cook_time}

        ## 🥕 Ingredients
        - ingredient 1
        - ingredient 2
        - ingredient 3

        ## 📋 Instructions
        1. step 1
        2. step 2
        3. step 3

        ## ⭐️ Tips
        - tip 1
        - tip 2

        ## 🥗 Nutrition
        - Calories: {calories}
        - Protein: {protein} g
        - Carbs: {carbs} g
        - Fat: {fat} g
    """.trimIndent()
