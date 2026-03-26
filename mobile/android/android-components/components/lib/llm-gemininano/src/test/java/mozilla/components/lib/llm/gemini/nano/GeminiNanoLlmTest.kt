/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.gemini.nano

import com.google.mlkit.genai.common.FeatureStatus
import com.google.mlkit.genai.common.GenAiException
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.lib.llm.gemini.nano.fakes.FakeGenerativeModel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

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
    fun `prompt returns Failure when GenAiException is thrown`() = runTest {
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.AVAILABLE),
            exception = GenAiException(null, GenAiException.ErrorCode.REQUEST_PROCESSING_ERROR),
        )

        val llm = GeminiNanoLlm(buildModel = { fakeModel })

        val results = llm.prompt(Prompt("test prompt")).toList()

        assertEquals(1, results.size)
        val failure = results[0] as Llm.Response.Failure
        assertEquals(0, failure.exception.errorCode.value)
        assertTrue(failure.exception.message?.startsWith("Gemini Nano inference failed:") == true)
    }

    @Test
    fun `logger delivers useful messaging during prompt and download flow`() = runTest {
        val logMessages = mutableListOf<String>()
        val prompt = "test"
        val fakeModel = FakeGenerativeModel(
            status = sequenceOf(FeatureStatus.AVAILABLE),
            responseMap = mapOf(prompt to listOf("response")),
        )

        val llm = GeminiNanoLlm(
            buildModel = { fakeModel },
            logger = { logMessages.add(it) },
        )

        llm.prompt(Prompt(prompt)).toList()

        assertEquals(2, logMessages.size)
        assertTrue(logMessages[0].contains("Beginning model response stream"))
        assertTrue(logMessages[1].contains("Model stream completed with:"))
    }
}
