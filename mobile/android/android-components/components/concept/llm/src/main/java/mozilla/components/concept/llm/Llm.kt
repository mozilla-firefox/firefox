/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

import kotlinx.coroutines.flow.Flow

/**
 * A value type representing a prompt that can be delivered to a LLM.
 */
@JvmInline
value class Prompt(val value: String)

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
     * A prompt request delivered to the LLM for inference.
     *
     * @param prompt a [Prompt] that will be sent to the [Llm].
     * @return a [Flow] of [String] of the response from the [Llm].
     */
    suspend fun prompt(prompt: Prompt): Flow<String>

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
