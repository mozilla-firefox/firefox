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
     * When [ContextWindow.tools] is non-empty, the result may be a [LlmTurnResult.ToolCalls]
     * rather than a [LlmTurnResult.Text].
     *
     * @param contextWindow A [ContextWindow] containing the ordered conversation messages and
     *   any tools available for this turn.
     * @return A [LlmTurnResult] that is either a text token stream or a set of tool calls.
     */
    suspend fun prompt(contextWindow: ContextWindow): LlmTurnResult

    /**
     * Represents the full context provided to an [Llm] for a single inference request.
     *
     * @param messages The conversation history, in chronological order.
     * @param tools Tools available for this turn. Empty means no tool calling.
     */
    data class ContextWindow(
        val messages: List<Message>,
        val tools: List<LlmTool> = emptyList(),
    )

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

        /**
         * An assistant turn that requested one or more tool calls instead of producing text.
         *
         * @param calls The tool calls the model requested.
         */
        data class AssistantToolCall(val calls: List<ToolCall>) : Message() {
            override val message: String = ""
        }

        /**
         * A tool result fed back to the model after executing a tool call.
         *
         * @param toolCallId The ID of the [ToolCall] this result corresponds to.
         * @param message The result content returned by the tool.
         */
        data class Tool(val toolCallId: String, override val message: String) : Message()
    }

    /**
     * A single tool call requested by the model during a turn.
     *
     * @property id Unique identifier used to correlate this call with its result.
     * @property toolName The name of the tool to invoke.
     * @property arguments A JSON string of the arguments the model supplied.
     */
    data class ToolCall(val id: String, val toolName: String, val arguments: String)

    /**
     * The result of a single model turn.
     */
    sealed class LlmTurnResult {
        /** The model produced a streaming text response. */
        data class Text(val flow: Flow<String>) : LlmTurnResult()

        /** The model requested one or more tool calls instead of producing text. */
        data class ToolCalls(val calls: List<ToolCall>) : LlmTurnResult()
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
