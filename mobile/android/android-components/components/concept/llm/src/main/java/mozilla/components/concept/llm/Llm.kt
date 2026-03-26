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
     * A prompt request delivered to the LLM for inference, which will stream a series
     * of [Response]s as they are made available.
     */
    suspend fun prompt(prompt: Prompt): Flow<Response>

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

    /**
     * A response from prompting a LLM.
     */
    sealed class Response {

        /**
         * A successful response from the LLM has occurred. This may include partial data,
         * or be an indication that the reply has completed.
         */
        sealed class Success : Response() {
            /**
             * A (potentially) partial reply from the LLM. This may be a complete reply if
             * it is short or the underlying implementation does not stream responses.
             */
            data class ReplyPart(val value: String) : Success()

            /**
             * An indication that the reply from the LLM is finishes.
             */
            data object ReplyFinished : Success()
        }

        /**
         * A failure response from a LLM.
         */
        data class Failure(val exception: Llm.Exception) : Response()
    }
}
