/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

/**
 * Strategy that fits a [Llm.ContextWindow] within a model's token limit.
 *
 * The session invokes [trim] before every [Llm.prompt] call. Implementations may
 * drop old messages, summarize them, or apply any other reduction policy.
 */
interface ContextWindowStrategy {
    /**
     * Returns a [Llm.ContextWindow] that fits within [llm]'s context window.
     *
     * @param contextWindow The full context to potentially trim.
     * @param llm The model that will receive the trimmed context; use
     *   [Llm.countTokens] and [Llm.contextWindowSize] to measure and bound.
     */
    suspend fun trim(contextWindow: Llm.ContextWindow, llm: Llm): Llm.ContextWindow

    companion object {
        /**
         * Preserves all system messages and drops the oldest user/assistant messages
         * one at a time until the context fits within [Llm.contextWindowSize].
         */
        val Default: ContextWindowStrategy = object : ContextWindowStrategy {
            override suspend fun trim(contextWindow: Llm.ContextWindow, llm: Llm): Llm.ContextWindow {
                if (llm.countTokens(contextWindow) <= llm.contextWindowSize) return contextWindow

                val systemMessages = contextWindow.messages.filterIsInstance<Llm.Message.System>()
                val conversation = contextWindow.messages
                    .filter { it !is Llm.Message.System }
                    .toMutableList()

                while (conversation.isNotEmpty()) {
                    conversation.removeAt(0)
                    val candidate = Llm.ContextWindow(systemMessages + conversation)
                    if (llm.countTokens(candidate) <= llm.contextWindowSize) return candidate
                }
                return Llm.ContextWindow(systemMessages)
            }
        }
    }
}
