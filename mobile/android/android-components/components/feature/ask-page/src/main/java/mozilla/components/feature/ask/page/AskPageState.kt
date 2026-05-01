/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import mozilla.components.concept.llm.Llm
import mozilla.components.lib.state.State
import mozilla.components.ui.richtext.ir.RichDocument

/**
 * An in-progress assistant response.
 *
 * @param rawMarkdown The accumulated raw markdown text received so far.
 * @param richDocument The incrementally-parsed [RichDocument] for display.
 */
data class PartialResponse(val rawMarkdown: String, val richDocument: RichDocument)

/**
 * The [State] of the [AskPageStore].
 */
sealed class AskPageState : State {

    /** The feature is idle before the UI has appeared. */
    data object Idle : AskPageState()

    /**
     * The session is available and waiting for user input.
     *
     * @param messages The conversation history to display, in chronological order.
     * @param hasError Whether the last LLM call failed. Cleared on the next [UserMessageSubmitted].
     */
    data class Ready(
        val messages: List<Llm.Message> = emptyList(),
        val hasError: Boolean = false,
    ) : AskPageState()

    /**
     * A message has been sent and the session is waiting for the first response token.
     *
     * @param messages The conversation history to display, in chronological order.
     */
    data class Waiting(
        val messages: List<Llm.Message>,
    ) : AskPageState()

    /**
     * The LLM is streaming a response.
     *
     * @param messages The conversation history to display, in chronological order.
     * @param pendingResponse The in-progress [PartialResponse] being streamed.
     */
    data class Receiving(
        val messages: List<Llm.Message>,
        val pendingResponse: PartialResponse,
    ) : AskPageState()

    /** The ask page UI was dismissed. */
    data object Finished : AskPageState()
}
