/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.gemini.nano

import com.google.common.util.concurrent.ListenableFuture
import com.google.mlkit.genai.common.DownloadCallback
import com.google.mlkit.genai.common.DownloadStatus
import com.google.mlkit.genai.common.FeatureStatus
import com.google.mlkit.genai.common.GenAiException
import com.google.mlkit.genai.common.StreamingCallback
import com.google.mlkit.genai.prompt.Candidate
import com.google.mlkit.genai.prompt.CountTokensResponse
import com.google.mlkit.genai.prompt.GenerateContentRequest
import com.google.mlkit.genai.prompt.GenerateContentResponse
import com.google.mlkit.genai.prompt.GenerativeModel
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.`when`
import java.util.concurrent.ExecutorService

class GeminiNanoLlmTest {

    @Test
    fun `prompt returns Success when model is AVAILABLE and processes without error`() = runTest {
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.AVAILABLE),
            responseMap = mapOf("test prompt" to listOf("test response")),
        )

        val llm = GeminiNanoLlm(buildModel = { fakeModel })

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(2, results.size)
        assertEquals(Llm.Response.Success.ReplyPart("test response"), results[0])
        assertEquals(Llm.Response.Success.ReplyFinished, results[1])
        assertEquals("test prompt", fakeModel.lastPromptProcessed)
    }

    @Test
    fun `prompt waits for download when model is DOWNLOADING`() = runTest {
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.DOWNLOADING, FeatureStatus.AVAILABLE),
            responseMap = mapOf("test prompt" to listOf("response after download")),
        )

        val llm = GeminiNanoLlm(buildModel = { fakeModel })

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(3, results.size)
        assertEquals(Llm.Response.Success.ReplyPart("response after download"), results[1])
        assertEquals(Llm.Response.Success.ReplyFinished, results[2])
        assertEquals("test prompt", fakeModel.lastPromptProcessed)
    }

    @Test
    fun `prompt downloads and returns Success when model is DOWNLOADABLE`() = runTest {
        val logMessages = mutableListOf<String>()
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.DOWNLOADABLE),
            downloadFlow = flowOf(
                DownloadStatus.DownloadProgress(50),
                DownloadStatus.DownloadCompleted,
            ),
            responseMap = mapOf("test prompt" to listOf("downloaded response")),
        )

        val llm = GeminiNanoLlm(
            buildModel = { fakeModel },
            logger = { logMessages.add(it) },
        )

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(3, results.size)
        assertEquals(Llm.Response.Success.ReplyPart("downloaded response"), results[1])
        assertEquals(Llm.Response.Success.ReplyFinished, results[2])
        assertEquals(4, logMessages.size)
        assertEquals("Download update: DownloadProgress(totalBytesDownloaded=50)", logMessages[0])
        assertTrue(logMessages[1].contains("DownloadCompleted"))
        assertTrue(logMessages[3].contains("Model stream completed with:"))
    }

    @Test
    fun `prompt returns Failure after failed download when DOWNLOADABLE`() = runTest {
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.DOWNLOADABLE),
            downloadFlow = flowOf(
                DownloadStatus.DownloadProgress(25),
                DownloadStatus.DownloadFailed(GenAiException(null, GenAiException.ErrorCode.UNKNOWN)),
            ),
        )

        val llm = GeminiNanoLlm(buildModel = { fakeModel })

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(2, results.size)
        assertTrue(results[1] is Llm.Response.Failure)
    }

    @Test
    fun `concurrent prompt calls during download share download flow and complete with own prompts`() = runTest {
        val sharedModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.DOWNLOADABLE, FeatureStatus.DOWNLOADING, FeatureStatus.AVAILABLE),
            downloadFlow = flow {
                emit(DownloadStatus.DownloadProgress(50))
                delay(50)
                emit(DownloadStatus.DownloadProgress(100))
                delay(50)
                emit(DownloadStatus.DownloadCompleted)
            },
            responseMap = mapOf(
                "first prompt" to listOf("response for first prompt"),
                "second prompt" to listOf("response for second prompt"),
            ),
        )

        val llm = GeminiNanoLlm(buildModel = { sharedModel })

        val firstCall = async { llm.prompt(Prompt("first prompt")).toList() }
        delay(25)
        val secondCall = async { llm.prompt(Prompt("second prompt")).toList() }

        val firstResults = firstCall.await()
        val secondResults = secondCall.await()

        assertEquals(3, firstResults.size)
        assertEquals(Llm.Response.Success.ReplyPart("response for first prompt"), firstResults[1])
        assertEquals(Llm.Response.Success.ReplyFinished, firstResults[2])

        assertEquals(3, secondResults.size)
        assertEquals(Llm.Response.Success.ReplyPart("response for second prompt"), secondResults[1])
        assertEquals(Llm.Response.Success.ReplyFinished, secondResults[2])

        assertTrue(sharedModel.prompts.contains("first prompt"))
        assertTrue(sharedModel.prompts.contains("second prompt"))
        assertEquals(2, sharedModel.prompts.size)
    }

    @Test
    fun `prompt returns Failure when model status is unavailable`() = runTest {
        val fakeModel = FakeGenerativeModel(status = sequenceOf(FeatureStatus.UNAVAILABLE))

        val llm = GeminiNanoLlm(buildModel = { fakeModel })

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(1, results.size)
        assertEquals(Llm.Response.Failure("Unavailable"), results[0])
    }

    @Test
    fun `prompt returns Failure when GenAiException is thrown`() = runTest {
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.AVAILABLE),
            exception = GenAiException(null, GenAiException.ErrorCode.REQUEST_PROCESSING_ERROR),
        )

        val llm = GeminiNanoLlm(buildModel = { fakeModel })

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(1, results.size)
        assertEquals(Llm.Response.Failure("Gemini Nano inference failed: [ErrorCode 4] Request doesn't pass certain policy check. Please try a different input."), results[0])
    }

    @Test
    fun `logger delivers useful messaging during prompt and download flow`() = runTest {
        val logMessages = mutableListOf<String>()
        val prompt = "test"
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.DOWNLOADABLE),
            downloadFlow = flowOf(
                DownloadStatus.DownloadProgress(10),
                DownloadStatus.DownloadProgress(50),
                DownloadStatus.DownloadProgress(90),
                DownloadStatus.DownloadCompleted,
            ),
            responseMap = mapOf(prompt to listOf("response")),
        )

        val llm = GeminiNanoLlm(
            buildModel = { fakeModel },
            logger = { logMessages.add(it) },
        )

        llm.prompt(Prompt(prompt)).toList()

        assertEquals(6, logMessages.size)
        assertTrue(logMessages[0].contains("DownloadProgress(totalBytesDownloaded=10)"))
        assertTrue(logMessages[1].contains("DownloadProgress(totalBytesDownloaded=50)"))
        assertTrue(logMessages[2].contains("DownloadProgress(totalBytesDownloaded=90)"))
        assertTrue(logMessages[3].contains("DownloadCompleted"))
        assertTrue(logMessages[4].contains("Beginning model response stream"))
        assertTrue(logMessages[5].contains("Model stream completed with:"))
    }

    private class FakeGenerativeModel(
        status: Sequence<Int>,
        private val downloadFlow: Flow<DownloadStatus> = flowOf(DownloadStatus.DownloadCompleted),
        private val responseMap: Map<String, List<String>> = emptyMap(),
        private val exception: GenAiException? = null,
    ) : GenerativeModel {
        var lastPromptProcessed: String? = null
        val prompts = mutableListOf<String>()
        private val statusIterator = status.iterator()

        override suspend fun checkStatus(): Int = statusIterator.next()

        override fun generateContentStream(prompt: String): Flow<GenerateContentResponse> {
            lastPromptProcessed = prompt
            prompts.add(prompt)
            if (exception != null) {
                throw exception
            }

            val responseTextParts = responseMap[prompt]!!
            return responseTextParts.mapIndexed { idx, text ->
                val mockCandidate: Candidate = mock()
                `when`(mockCandidate.text).thenReturn(text)
                if (idx == responseTextParts.size - 1) {
                    `when`(mockCandidate.finishReason).thenReturn(Candidate.FinishReason.STOP)
                }
                val mockResponse = mock<GenerateContentResponse>()
                `when`(mockResponse.candidates).thenReturn(listOf(mockCandidate))
                mockResponse
            }.asFlow()
        }

        override suspend fun generateContent(prompt: String): GenerateContentResponse {
            TODO("Not yet implemented")
        }

        override suspend fun getBaseModelName(): String {
            TODO("Not yet implemented")
        }

        override fun download(): Flow<DownloadStatus> = downloadFlow

        override fun downloadForFutures(callback: DownloadCallback): ListenableFuture<Void> {
            TODO("Not yet implemented")
        }

        override suspend fun warmup() {
            TODO("Not yet implemented")
        }

        override fun warmupForFutures(): ListenableFuture<Void> {
            TODO("Not yet implemented")
        }

        override suspend fun countTokens(request: GenerateContentRequest): CountTokensResponse {
            TODO("Not yet implemented")
        }

        override fun countTokensForFutures(request: GenerateContentRequest): ListenableFuture<CountTokensResponse> {
            TODO("Not yet implemented")
        }

        override suspend fun getTokenLimit(): Int {
            TODO("Not yet implemented")
        }

        override fun getTokenLimitForFutures(): ListenableFuture<Int> {
            TODO("Not yet implemented")
        }

        override suspend fun generateContent(request: GenerateContentRequest): GenerateContentResponse {
            TODO("Not yet implemented")
        }

        override suspend fun generateContent(
            request: GenerateContentRequest,
            streamingCallback: StreamingCallback,
        ): GenerateContentResponse {
            TODO("Not yet implemented")
        }

        override fun generateContentStream(request: GenerateContentRequest): Flow<GenerateContentResponse> {
            TODO("Not yet implemented")
        }

        override suspend fun clearCaches() {
            TODO("Not yet implemented")
        }

        override fun clearCachesForFutures(): ListenableFuture<Void> {
            TODO("Not yet implemented")
        }

        override fun close() {
            TODO("Not yet implemented")
        }

        override fun getWorkerExecutor(): ExecutorService {
            TODO("Not yet implemented")
        }
    }
}
