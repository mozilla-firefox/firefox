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
    is ViewAppeared -> AskPageState.Ready()
    is ViewDismissed -> AskPageState.Finished
    is UserMessageSubmitted -> when (state) {
        is AskPageState.Ready -> AskPageState.Waiting(
            messages = state.messages + Llm.Message.User(action.text),
        )
        else -> state
    }
    is ReceivedParsedResponse -> when (state) {
        is AskPageState.Waiting -> AskPageState.Receiving(
            messages = state.messages,
            pendingResponse = action.partialResponse,
        )
        is AskPageState.Receiving -> state.copy(pendingResponse = action.partialResponse)
        else -> state
    }
    is ResponseCompleted -> when (state) {
        is AskPageState.Waiting -> AskPageState.Ready(messages = state.messages)
        is AskPageState.Receiving -> AskPageState.Ready(
            messages = state.messages + Llm.Message.Assistant(state.pendingResponse.rawMarkdown),
        )
        else -> state
    }
    is ResponseFailed -> when (state) {
        is AskPageState.Waiting -> AskPageState.Ready(messages = state.messages, error = action.throwable)
        is AskPageState.Receiving -> AskPageState.Ready(messages = state.messages, error = action.throwable)
        else -> state
    }
}
