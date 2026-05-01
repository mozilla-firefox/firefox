/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import mozilla.components.concept.llm.Llm

/**
 * Reduces the given [action] and current [state] into a new [AskPageState].
 *
 * @param state The current [AskPageState].
 * @param action The [AskPageAction] to process.
 * @return The resulting [AskPageState] after applying the action.
 */
fun askPageReducer(state: AskPageState, action: AskPageAction): AskPageState = when (action) {
    is ViewAppeared -> AskPageState.Idle
    is ViewDismissed -> AskPageState.Finished
    is UserMessageSubmitted -> when (state) {
        is AskPageState.Ready -> state.copy(
            messages = state.messages + Llm.Message.User(action.text),
        )
        else -> AskPageState.WaitingToSendMessage(action.text)
    }
    is LlmProviderAction.ProviderAvailable -> state
    is LlmProviderAction.ProviderReady -> when (state) {
        is AskPageState.Idle -> AskPageState.Ready(llm = action.llm)
        is AskPageState.WaitingToSendMessage -> AskPageState.Ready(
            llm = action.llm,
            messages = listOf(Llm.Message.User(state.message)),
        )
        else -> state
    }
    is LlmProviderAction.ProviderFailed -> state
    is ReceivedParsedResponse -> when (state) {
        is AskPageState.Ready -> AskPageState.Receiving(
            llm = state.llm,
            messages = state.messages,
            pendingResponse = action.partialResponse,
        )
        is AskPageState.Receiving -> state.copy(pendingResponse = action.partialResponse)
        else -> state
    }
    is ResponseCompleted -> when (state) {
        is AskPageState.Receiving -> AskPageState.Ready(
            llm = state.llm,
            messages = state.messages + Llm.Message.Assistant(state.pendingResponse.rawMarkdown),
        )
        else -> state
    }
    is ResponseFailed -> when (state) {
        is AskPageState.Receiving -> AskPageState.Ready(
            llm = state.llm,
            messages = state.messages,
        )
        else -> state
    }
}
