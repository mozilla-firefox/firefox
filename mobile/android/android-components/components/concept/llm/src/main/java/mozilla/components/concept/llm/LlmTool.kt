/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

/**
 * A tool that an [LlmSession] can invoke on behalf of the model.
 *
 * Tools are registered at session construction time via [LlmSessionConfig.tools]. When the
 * model requests a tool call the session executes [execute] and feeds the result back
 * automatically, hiding the round-trip from the consumer.
 */
interface LlmTool {
    /** Unique name the model uses to identify this tool. */
    val name: String

    /** Human-readable description sent to the model to explain the tool's purpose. */
    val description: String

    /**
     * JSON Schema string describing the tool's input parameters.
     * The model generates arguments conforming to this schema.
     */
    val parametersSchema: String

    /**
     * Executes the tool with the given [arguments].
     *
     * @param arguments A JSON string containing the model-supplied argument values.
     * @return A plain-text result that is fed back to the model as a tool response.
     */
    suspend fun execute(arguments: String): String
}
