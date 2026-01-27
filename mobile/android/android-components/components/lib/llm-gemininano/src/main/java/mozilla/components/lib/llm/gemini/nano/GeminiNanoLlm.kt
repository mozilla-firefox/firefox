/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.gemini.nano

import com.google.mlkit.genai.common.DownloadStatus
import com.google.mlkit.genai.common.FeatureStatus
import com.google.mlkit.genai.common.GenAiException
import com.google.mlkit.genai.prompt.Generation
import com.google.mlkit.genai.prompt.GenerativeModel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.FlowCollector
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.support.base.log.logger.Logger

/**
 * An instance of a LLM that uses local, on-device capabilities provided by Gemini Nano to handle
 * inference.
 */
class GeminiNanoLlm(
    private val buildModel: () -> GenerativeModel = { Generation.getClient() },
    private val logger: (String) -> Unit = { message -> Logger("mozac/GeminiNanoLlm").info(message) },
) : Llm {

    private val model by lazy {
        buildModel()
    }

    private val downloadMutex = Mutex()

    override suspend fun prompt(prompt: Prompt): Flow<Llm.Response> = flow {
        val model = model
        when (model.checkStatus()) {
            FeatureStatus.AVAILABLE -> {
                streamPromptResponses(prompt)
            }
            FeatureStatus.DOWNLOADING -> {
                emit(Llm.Response.Preparing("Downloading model"))
                // await the completion of the ongoing download
                downloadMutex.withLock {
                    if (model.checkStatus() == FeatureStatus.AVAILABLE) {
                        streamPromptResponses(prompt)
                    } else {
                        emit(Llm.Response.Failure("Model should be downloaded and is not"))
                    }
                }
            }
            FeatureStatus.DOWNLOADABLE -> {
                emit(Llm.Response.Preparing("Downloading model"))
                val result = downloadMutex.withLock {
                     model.download().onEach { status ->
                        logger("Download update: $status")
                    }.first { status ->
                        status == DownloadStatus.DownloadCompleted || status is DownloadStatus.DownloadFailed
                    }
                }
                if (result is DownloadStatus.DownloadFailed) {
                    val message = "Download failed ${result.e.message}"
                    logger(message)
                    emit(Llm.Response.Failure(message))
                } else {
                    streamPromptResponses(prompt)
                }
            }
            else -> emit(Llm.Response.Failure("Unavailable"))
        }
    }

    private suspend fun FlowCollector<Llm.Response>.streamPromptResponses(prompt: Prompt) = try {
        // consume replies from the model until it provides a finish reason
        logger("Beginning model response stream")
        model.generateContentStream(prompt.value).onEach { response ->
            emit(Llm.Response.Success.ReplyPart(response.candidates[0].text))
        }.first {
            val finishReason = it.candidates[0].finishReason
            (finishReason != null).also {
                logger("Model stream completed with: $finishReason")
            }
        }
        emit(Llm.Response.Success.ReplyFinished)
    } catch (e: GenAiException) {
        val message = "Gemini Nano inference failed: ${e.message}"
        logger(message)
        emit(Llm.Response.Failure(message))
    }
}
