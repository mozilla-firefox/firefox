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

    /** The feature is idle, waiting for the LLM provider to become ready. */
    data object Idle : AskPageState()

    /**
     * The user has submitted a message but the LLM provider is not yet ready.
     *
     * @param message The message the user submitted, to be sent once the LLM is ready.
     */
    data class WaitingToSendMessage(val message: String) : AskPageState()

    /**
     * The LLM is ready and waiting for user input.
     *
     * @param llm The ready [Llm] instance.
     * @param messages The ordered list of [Llm.Message]s in the current conversation, used as the
     *   context window for the LLM.
     */
    data class Ready(
        val llm: Llm,
        val messages: List<Llm.Message> = emptyList(),
    ) : AskPageState()

    /**
     * The LLM is streaming a response.
     *
     * @param llm The ready [Llm] instance.
     * @param messages The ordered list of [Llm.Message]s in the current conversation, used as the
     *   context window for the LLM.
     * @param pendingResponse The in-progress [PartialResponse] being streamed.
     */
    data class Receiving(
        val llm: Llm,
        val messages: List<Llm.Message>,
        val pendingResponse: PartialResponse,
    ) : AskPageState()

    /** */
    data object Finished : AskPageState()
}
