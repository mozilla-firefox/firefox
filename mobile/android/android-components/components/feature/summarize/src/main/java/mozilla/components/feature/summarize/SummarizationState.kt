/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.lib.state.State

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
     * Summarization is in progress.
     *
     * @param parts the parts that we've generated so far.
     */
    data class Summarizing(val parts: List<String> = listOf()) : SummarizationState()

    /**
     * Summarization completed successfully.
     *
     * @param text The generated summary.
     */
    data class Summarized(val text: String) : SummarizationState()

    /**
     * An error occurred during the summarization lifecycle.
     *
     * @param error The [SummarizationError] describing what went wrong.
     */
    data class Error(val error: SummarizationError) : SummarizationState()

    /** User is finished with the Summarization Flow */
    sealed class Finished : SummarizationState() {
        /** User finished by canceling the flow. */
        data object Cancelled : Finished()

        /** User finished by clicking Learn More in the shake consent screen. */
        data object LearnMoreAboutShakeConsent : Finished()
    }

    companion object {
        val initial: SummarizationState get() = Inert(false)
    }
}

/**
* Describes the possible failure modes of the summarization feature.
*/
sealed class SummarizationError {
    /** The user declined the consent prompt. */
    data object ConsentDenied : SummarizationError()

    /** The page content could not be extracted or is unavailable. */
    data object ContentUnavailable : SummarizationError()

    /** The page content is too short to summarize. */
    data object ContentTooShort : SummarizationError()

    /** The page content exceeds the maximum supported length. */
    data object ContentTooLong : SummarizationError()

    /** The user declined to download the summarization model. */
    data object DownloadDenied : SummarizationError()

    /** The model download did not complete successfully. */
    data object DownloadFailed : SummarizationError()

    /** The model download was cancelled before completion. */
    data object DownloadCancelled : SummarizationError()

    /** The summarization model failed to produce a result. */
    data object SummarizationFailed : SummarizationError()

    /** The model produced a result that could not be used as a valid summary. */
    data object InvalidSummary : SummarizationError()

    /** A network error occurred during download or summarization. */
    data object NetworkError : SummarizationError()
}
