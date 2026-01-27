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
 * An abstract definition of a LLM that can receive prompts.
 */
interface Llm {
    /**
     * A prompt request delivered to the LLM for inference, which will stream a series
     * of [Response]s as they are made available.
     */
    suspend fun prompt(prompt: Prompt): Flow<Response>

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
         * The LLM is engaged in getting ready to receive prompts. This may include actions like
         * authenticating with a remote or downloading a local model.
         */
        data class Preparing(val status: String) : Response()

        /**
         * A failure response from a LLM.
         */
        data class Failure(val reason: String) : Response()
    }
}
