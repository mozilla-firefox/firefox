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
    is SummarizationRequested -> SummarizationState.Loading(action.info)
    is ReceivedLlmResponse -> state.applyResponse(action.response)
    is ReceivedParsedDocument -> state.updateDocument(action.document)
    is SettingsClicked -> when (state) {
        is SummarizationState.Summarized -> SummarizationState.Settings(info = state.info, document = state.document)
        else -> state
    }
    is SettingsBackClicked -> when (state) {
        is SummarizationState.Settings -> SummarizationState.Summarized(info = state.info, document = state.document)
        else -> state
    }
    is SummarizationFailed -> SummarizationState.Error(SummarizationError.SummarizationFailed(action.exception))
    is LlmProviderAction.ProviderFailed -> SummarizationState.Error(
        SummarizationError.SummarizationFailed(action.exception),
    )
    else -> state
}

internal fun SummarizationState.applyResponse(response: Llm.Response): SummarizationState {
    if (this !is SummarizationState.Summarizing) return this
    return when (response) {
        is Llm.Response.Failure -> {
            val contentTooLongErrorCode = 1005
            when (response.exception.errorCode.value) {
                contentTooLongErrorCode -> SummarizationState.Error(SummarizationError.ContentTooLong)
                else -> SummarizationState.Error(
                    SummarizationError.SummarizationFailed(response.exception),
                )
            }
        }
        Llm.Response.Success.ReplyFinished -> SummarizationState.Summarized(info = info, document)
        else -> this
    }
}

internal fun SummarizationState.updateDocument(document: RichDocument): SummarizationState {
    return when (this) {
        is SummarizationState.Loading -> SummarizationState.Summarizing(info, document)
        is SummarizationState.Summarizing -> copy(document = document)
        else -> this
    }
}
