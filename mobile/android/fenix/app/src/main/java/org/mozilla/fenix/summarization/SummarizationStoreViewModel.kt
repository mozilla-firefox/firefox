/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.feature.summarize.ErrorReporter
import mozilla.components.feature.summarize.SummarizationMiddleware
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummarizationStore
import mozilla.components.feature.summarize.content.PageContentExtractor
import mozilla.components.feature.summarize.content.PageMetadataExtractor
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.feature.summarize.summarizationReducer

/**
 * A [ViewModel] that owns and survives configuration changes for a [SummarizationStore].
 *
 * @param initializedFromShake Whether the summarization feature was triggered by a shake gesture.
 * @param llmProvider the [LlmProvider] used to summarize the page.
 * @param settings the SummarizationSettings.
 * @param pageContentExtractor an extractor for page content.
 * @param pageMetadataExtractor an extractor for page metadata.
 * @param errorReporter reports caught exceptions to the crash reporting service.
 */
class SummarizationStoreViewModel(
    initializedFromShake: Boolean,
    llmProvider: CloudLlmProvider,
    settings: SummarizationSettings,
    pageContentExtractor: PageContentExtractor,
    pageMetadataExtractor: PageMetadataExtractor,
    errorReporter: ErrorReporter,
) : ViewModel() {
    val store = SummarizationStore(
        initialState = SummarizationState.Inert(initializedFromShake),
        reducer = ::summarizationReducer,
        middleware = listOf(
            SummarizationMiddleware(
                settings = settings,
                llmProvider = llmProvider,
                pageContentExtractor = pageContentExtractor,
                pageMetadataExtractor = pageMetadataExtractor,
                errorReporter = errorReporter,
                scope = viewModelScope,
            ),
        ),
    )

    companion object {
        /**
         * Creates a [ViewModelProvider.Factory] for [SummarizationStoreViewModel].
         *
         * @param initializedFromShake Whether the summarization feature was triggered by a shake gesture.
         * @param llmProvider the [LlmProvider] used to summarize the page.
         * @param settings the SummarizationSettings.
         * @param pageContentExtractor an extractor for page content.
         * @param pageMetadataExtractor an extractor for page metadata.
         * @param errorReporter reports caught exceptions to the crash reporting service.
         */
        fun factory(
            initializedFromShake: Boolean,
            llmProvider: CloudLlmProvider,
            settings: SummarizationSettings,
            pageContentExtractor: PageContentExtractor,
            pageMetadataExtractor: PageMetadataExtractor,
            errorReporter: ErrorReporter,
        ) = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                return SummarizationStoreViewModel(
                    initializedFromShake,
                    llmProvider = llmProvider,
                    settings = settings,
                    pageContentExtractor = pageContentExtractor,
                    pageMetadataExtractor = pageMetadataExtractor,
                    errorReporter = errorReporter,
                ) as T
            }
        }
    }
}
