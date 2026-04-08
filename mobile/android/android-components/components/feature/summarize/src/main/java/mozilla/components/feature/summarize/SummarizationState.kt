/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.feature.summarize.SummarizationState.Finished
import mozilla.components.lib.state.State
import mozilla.components.ui.richtext.ir.RichDocument

/**
 * The [State] of the [SummarizationStore]
 */
sealed class SummarizationState : State {
    /**
     * The feature is idle and not actively summarizing.
     *
     * @param initializedWithShake Whether the feature was first triggered via a shake gesture.
     */
    data class Inert(val initializedWithShake: Boolean) : SummarizationState()

    /** The user must consent to shake-to-summarize before proceeding. */
    data object ShakeConsentRequired : SummarizationState()

    /** The user must consent to both the shake gesture and a model download before proceeding. */
    data object ShakeConsentWithDownloadRequired : SummarizationState()

    /** The user must download an on-device model before continuing */
    data object DownloadConsentRequired : SummarizationState()

    /** The on-device model is downloading  */
    data class Downloading(val bytesToDownload: Float, val bytesDownloaded: Float) : SummarizationState() {
        val downloadProgress: Float get() = bytesToDownload / bytesToDownload
    }

    /**
     * We're waiting for a response from the [mozilla.components.concept.llm.Llm]
     *
     * @param info the information for the current [mozilla.components.concept.llm.Llm]
     */
    data class Loading(val info: LlmProvider.Info) : SummarizationState()

    /**
     * Summarization is in progress.
     *
     * @param info metadata about the LLM that generated the summary
     * @param document the document we've generated so far.
     */
    data class Summarizing(
        val info: LlmProvider.Info,
        val document: RichDocument = RichDocument(listOf()),
    ) : SummarizationState()

    /**
     * Summarization completed successfully.
     *
     * @param info metadata about the LLM that generated the summary
     * @param document The generated document.
     */
    data class Summarized(
        val info: LlmProvider.Info,
        val document: RichDocument = RichDocument(listOf()),
    ) : SummarizationState()

    /**
     * An error occurred during the summarization lifecycle.
     *
     * @param error The [SummarizationError] describing what went wrong.
     */
    data class Error(val error: SummarizationError) : SummarizationState()

    /**
     * The user is viewing the summarization settings.
     *
     * @param info metadata about the LLM that generated the summary
     * @param document The document to return to when navigating back.
     */
    data class Settings(val info: LlmProvider.Info, val document: RichDocument) : SummarizationState()

    /** User is finished with the Summarization Flow */
    sealed class Finished : SummarizationState() {
        /** User finished by canceling the flow. */
        data object Cancelled : Finished()
    }

    /** User clicked Learn More in the shake consent screen. */
    data object LearnMoreAboutShakeConsent : SummarizationState()

    companion object {
        val initial: SummarizationState get() = Inert(false)
    }
}

/**
* Describes the possible failure modes of the summarization feature.
*/
sealed class SummarizationError {
    /** The page content exceeds the maximum supported length. */
    data object ContentTooLong : SummarizationError()

    /** The model download did not complete successfully. */
    data object DownloadFailed : SummarizationError()

    /** The summarization model failed to produce a result. */
    data class SummarizationFailed(val exception: Llm.Exception) : SummarizationError()
}

val SummarizationState.isLoading get() = this is SummarizationState.Loading
val SummarizationState.isSummarizing get() = this is SummarizationState.Summarizing
val SummarizationState.isSummarized get() = this is SummarizationState.Summarized
