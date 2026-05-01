/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

import kotlinx.coroutines.flow.Flow

/**
 * An integer error code that can be used to categorize failures.
 */
@JvmInline
value class ErrorCode(val value: Int)

/**
 * An abstract definition of a LLM that can receive prompts.
 */
interface Llm {
    /**
     * The maximum number of tokens this model's context window can hold.
     * Defaults to [Int.MAX_VALUE] for implementations that do not enforce a limit.
     */
    val contextWindowSize: Int get() = Int.MAX_VALUE

    /**
     * Returns the number of tokens in [contextWindow] as counted by this model's tokenizer.
     * Defaults to a rough approximation of 4 characters per token.
     *
     * @param contextWindow The context to measure.
     */
    suspend fun countTokens(contextWindow: ContextWindow): Int =
        contextWindow.messages.sumOf { it.message.length } / 4

    /**
     * Runs inference with the given [contextWindow].
     *
     * @param contextWindow A [ContextWindow] containing the ordered conversation messages.
     * @return a [Flow] of [String] tokens emitted as the [Llm] produces its response.
     */
    suspend fun prompt(contextWindow: ContextWindow): Flow<String>

    /**
     * Represents the full context provided to an [Llm] for a single inference request,
     * as an ordered list of [Message]s.
     *
     * @param messages The conversation history, in chronological order.
     */
    data class ContextWindow(val messages: List<Message>)

    /**
     * A single message in a [ContextWindow].
     */
    sealed class Message {
        abstract val message: String

        /**
         * A system-level instruction that shapes model behavior.
         *
         * @param message The instruction text.
         */
        data class System(override val message: String) : Message()

        /**
         * A message from the end user.
         *
         * @param message The user's input text.
         */
        data class User(override val message: String) : Message()

        /**
         * A message from the assistant (model).
         *
         * @param message The assistant's response text.
         */
        data class Assistant(override val message: String) : Message()
    }

    /**
     * An exception thrown by an LLM, equipped with an [ErrorCode] to differentiate
     * error types. Implementation modules may subclass this to attach additional context.
     *
     * @param message A human-readable description of the failure.
     * @param errorCode The error code identifying the failure category.
     */
    open class Exception(
        message: String,
        val errorCode: ErrorCode,
    ) : kotlin.Exception(message) {
        companion object {
            /**
             * Create an unspecified error with the general error code.
             */
            fun unknown(message: String?) = Llm.Exception(
                message = message ?: "Unknown Llm Exception",
                errorCode = ErrorCode(0),
            )
        }
    }
}
