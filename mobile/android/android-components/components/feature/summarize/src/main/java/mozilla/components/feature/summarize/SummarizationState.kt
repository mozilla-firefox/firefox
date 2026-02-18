/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.lib.state.State

/**
 * The [State] of the [SummarizationStore]
 */
internal sealed class SummarizationState : State {
    data object Inert : SummarizationState()
    data object ShakeConsentRequired : SummarizationState()
    data object ShakeConsentWithDownloadRequired : SummarizationState()
    data object DownloadConsentRequired : SummarizationState()
    data class Downloading(val bytesToDownload: Float, val bytesDownloaded: Float) : SummarizationState() {
        val downloadProgress: Float get() = bytesToDownload / bytesToDownload
    }
    data object Summarizing : SummarizationState()
    data class Summarized(val text: String) : SummarizationState()
    data class Error(val error: SummarizationError) : SummarizationState()

    companion object {
        val initial: SummarizationState get() = Inert
    }
}

internal sealed class SummarizationError {
    data object ConsentDenied : SummarizationError()
    data object ContentUnavailable : SummarizationError()
    data object ContentTooShort : SummarizationError()
    data object ContentTooLong : SummarizationError()
    data object DownloadDenied : SummarizationError()
    data object DownloadFailed : SummarizationError()
    data object DownloadCancelled : SummarizationError()
    data object SummarizationFailed : SummarizationError()
    data object InvalidSummary : SummarizationError()
    data object NetworkError : SummarizationError()
}
