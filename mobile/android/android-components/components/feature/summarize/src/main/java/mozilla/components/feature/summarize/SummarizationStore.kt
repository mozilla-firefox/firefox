/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.lib.state.Action
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store

internal typealias SummarizationStore = Store<SummarizationState, SummarizationAction>

internal interface SummarizationAction : Action

internal data class SummarizationState(
    val pageSummarizationState: PageSummarizationState,
    val summarizedText: String = "",
) : State

internal sealed class PageSummarizationState {
    data object Inert : PageSummarizationState()
    data object WaitingForPermission : PageSummarizationState()
    data object Summarizing : PageSummarizationState()
    data class Summarized(val text: String) : PageSummarizationState()
    data class Error(val error: SummarizationError) : PageSummarizationState()
}

internal sealed class SummarizationError {
    object PermissionDenied : SummarizationError()
    object ContentUnavailable : SummarizationError()
    object ContentTooShort : SummarizationError()
    object ContentTooLong : SummarizationError()
    object SummarizationFailed : SummarizationError()
    object InvalidSummary : SummarizationError()
    object NetworkError : SummarizationError()
}
