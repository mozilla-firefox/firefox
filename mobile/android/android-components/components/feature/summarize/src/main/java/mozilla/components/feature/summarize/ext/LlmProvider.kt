/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ext

import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.feature.summarize.LlmProviderAction
import mozilla.components.feature.summarize.SummarizationRequested

internal val CloudLlmProvider.fetchLlm get() = flow {
    emit(SummarizationRequested(info))
    emitAll(state.map { it.action })
}

internal val CloudLlmProvider.State.action get() = when (this) {
    CloudLlmProvider.State.Available -> LlmProviderAction.ProviderAvailable
    is CloudLlmProvider.State.Unavailable -> LlmProviderAction.ProviderFailed(exception)
    is CloudLlmProvider.State.Ready -> LlmProviderAction.ProviderInitialized(llm)
}
