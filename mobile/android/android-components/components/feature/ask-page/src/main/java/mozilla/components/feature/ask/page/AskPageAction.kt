/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import mozilla.components.concept.llm.Llm
import mozilla.components.lib.state.Action

/**
 * Actions for the [AskPageStore].
 */
sealed interface AskPageAction : Action

/** The ask page UI became visible. */
data object ViewAppeared : AskPageAction

/** The ask page UI was dismissed. */
data object ViewDismissed : AskPageAction

/**
 * The user submitted a prompt.
 *
 * @param text The text of the user's message.
 */
data class UserMessageSubmitted(val text: String) : AskPageAction

/** We've received an incremental [PartialResponse] from the LLM. */
data class ReceivedParsedResponse(val partialResponse: PartialResponse) : AskPageAction

/** The LLM has finished streaming its response. */
data object ResponseCompleted : AskPageAction

/** The LLM encountered an error. */
data class ResponseFailed(val throwable: Throwable) : AskPageAction

/** Actions tracking the lifecycle of the [Llm] provider. */
sealed interface LlmProviderAction : AskPageAction {

    /** The LLM provider is reachable and can be prepared for use. */
    data object ProviderAvailable : LlmProviderAction

    /** The LLM provider is fully initialized and ready to receive prompts. */
    data class ProviderReady(val llm: Llm) : LlmProviderAction

    /** The LLM provider is unavailable. */
    data class ProviderFailed(val exception: Llm.Exception) : LlmProviderAction
}
