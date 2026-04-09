/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.gemini.nano

import com.google.mlkit.genai.common.GenAiException
import com.google.mlkit.genai.prompt.Generation
import com.google.mlkit.genai.prompt.GenerativeModel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.FlowCollector
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.onEach
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.support.base.log.logger.Logger

/**
 * An instance of a LLM that uses local, on-device capabilities provided by Gemini Nano to handle
 * inference.
 */
internal class GeminiNanoLlm(
    private val buildModel: () -> GenerativeModel = { Generation.getClient() },
    private val logger: (String) -> Unit = { message -> Logger("mozac/GeminiNanoLlm").info(message) },
) : Llm {

    private val model by lazy {
        buildModel()
    }

    override suspend fun prompt(prompt: Prompt): Flow<String> = flow {
        streamPromptResponses(prompt)
    }

    private suspend fun FlowCollector<String>.streamPromptResponses(prompt: Prompt) = try {
        // consume replies from the model until it provides a finish reason
        logger("Beginning model response stream")
        val content = listOfNotNull(prompt.systemPrompt, prompt.userPrompt).joinToString("\n\n")
        model.generateContentStream(content).onEach { response ->
            emit(response.candidates[0].text)
        }.first {
            val finishReason = it.candidates[0].finishReason
            (finishReason != null).also {
                logger("Model stream completed with: $finishReason")
            }
        }
    } catch (e: GenAiException) {
        val message = "Gemini Nano inference failed: ${e.message}"
        logger(message)
        throw Llm.Exception.unknown(message)
    }
}
