/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.concept.llm.Llm
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
    OffDeviceSummarizationShakeConsentAction.LearnMoreClicked -> SummarizationState.Finished.LearnMoreAboutShakeConsent
    OnDeviceSummarizationShakeConsentAction.LearnMoreClicked -> SummarizationState.Finished.LearnMoreAboutShakeConsent
    is LlmAction.SummarizationRequested -> SummarizationState.Summarizing(info = action.info)
    is LlmAction.ReceivedParsedDocument -> state.updateDocument(action.document)
    is LlmAction.SummarizationFinished -> state.finishSummarizing()
    is SettingsClicked -> when (state) {
        is SummarizationState.Summarized -> SummarizationState.Settings(info = state.info, document = state.document)
        else -> state
    }
    is SettingsBackClicked -> when (state) {
        is SummarizationState.Settings -> SummarizationState.Summarized(info = state.info, document = state.document)
        else -> state
    }
    is SummarizationFailed -> SummarizationState.Error(SummarizationError.SummarizationFailed(action.throwable))
    else -> state
}

internal fun SummarizationState.updateDocument(document: RichDocument): SummarizationState {
    return if (this is SummarizationState.Summarizing) {
        copy(document = document)
    } else {
        this
    }
}

internal fun SummarizationState.finishSummarizing(): SummarizationState {
    return if (this is SummarizationState.Summarizing) {
        SummarizationState.Summarized(info, document)
    } else {
        this
    }
}
