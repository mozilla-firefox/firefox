/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.ui.richtext.ir.RichDocument

/**
 * Reduces the given [action] and current [state] into a new [SummarizationState].
 *
 * @param state The current [SummarizationState].
 * @param action The [SummarizationAction] to process.
 * @return The resulting [SummarizationState] after applying the action.
 */
fun summarizationReducer(state: SummarizationState, action: SummarizationAction) = when (action) {
    is ShakeConsentRequested -> SummarizationState.ShakeConsentRequired
    OffDeviceSummarizationShakeConsentAction.CancelClicked -> SummarizationState.Finished.Cancelled
    OffDeviceSummarizationShakeConsentAction.LearnMoreClicked -> SummarizationState.LearnMoreAboutShakeConsent
    OnDeviceSummarizationShakeConsentAction.LearnMoreClicked -> SummarizationState.LearnMoreAboutShakeConsent
    ErrorAction.ErrorDismissed -> SummarizationState.Finished.ErrorDismissed
    is SummarizationRequested -> SummarizationState.Loading(action.info)
    is SummarizationCompleted -> state.complete()
    is SummarizationFailed -> SummarizationState.Error(SummarizationError.SummarizationFailed(action.exception))
    is ReceivedParsedDocument -> state.updateDocument(action.document)
    is SettingsClicked -> when (state) {
        is SummarizationState.Summarized ->
            SummarizationState.Settings(info = state.info, document = state.document, feedback = state.feedback)
        else -> state
    }
    is SettingsBackClicked -> when (state) {
        is SummarizationState.Settings ->
            SummarizationState.Summarized(info = state.info, document = state.document, feedback = state.feedback)
        else -> state
    }
    is SummaryFeedbackProvided -> when (state) {
        is SummarizationState.Summarized ->
            if (state.feedback == null) state.copy(feedback = action.feedback) else state
        else -> state
    }
    else -> state
}

private fun SummarizationState.complete(): SummarizationState {
    if (this !is SummarizationState.Summarizing) return this
    return SummarizationState.Summarized(info, document)
}

internal fun SummarizationState.updateDocument(document: RichDocument): SummarizationState {
    return when (this) {
        is SummarizationState.Loading -> SummarizationState.Summarizing(info, document)
        is SummarizationState.Summarizing -> copy(document = document)
        else -> this
    }
}
