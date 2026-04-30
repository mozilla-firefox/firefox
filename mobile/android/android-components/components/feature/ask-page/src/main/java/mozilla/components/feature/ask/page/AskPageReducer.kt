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
        is AskPageState.Active -> state.copy(
            messages = state.messages + Llm.Message.User(action.text),
        )
        else -> AskPageState.Active(messages = listOf(Llm.Message.User(action.text)))
    }
}
