/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.concept.llm.Llm

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
    is LlmAction.SummarizationRequested -> SummarizationState.Summarizing()
    is LlmAction.ReceivedResponse -> state.applyResponse(action.response)
    else -> { state }
}

internal fun SummarizationState.applyResponse(response: Llm.Response): SummarizationState {
    return if (this is SummarizationState.Summarizing) {
        when (response) {
            is Llm.Response.Failure -> SummarizationState.Summarized(response.reason)
            Llm.Response.Success.ReplyFinished -> SummarizationState.Summarized(text = parts.joinToString(""))
            is Llm.Response.Success.ReplyPart -> copy(parts = parts + response.value)
        }
    } else {
        this
    }
}
