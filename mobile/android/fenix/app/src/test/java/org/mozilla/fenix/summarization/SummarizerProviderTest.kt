/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.feature.summarize.ContentProperties
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SummarizerProviderTest {

    @Test
    fun `WHEN local llm is unavailable THEN provider returns failure`() = runTest {
        val result = summarizerProvider(
            FakeLlm(scriptedStatuses = listOf(Llm.Status.Unavailable)),
        )

        assertTrue(result.isFailure)
        assertEquals("Local LLM unavailable", result.exceptionOrNull()?.message)
    }

    @Test
    fun `WHEN local llm is available THEN provider returns summarizer`() = runTest {
        val result = summarizerProvider(FakeLlm())

        assertTrue(result.isSuccess)
        assertTrue(result.getOrNull() != null)
    }

    @Test
    fun `WHEN summarize is called THEN summarizer delegates prompt to local llm`() = runTest {
        val llm = FakeLlm()
        val summarizer = summarizerProvider(llm).getOrThrow()
        val article = "This is the article body."

        val responses = summarizer.summarize(article, DEFAULT_PROMPT).toList()
        assertEquals(
            listOf(
                Llm.Response.Success.ReplyPart("fake reply"),
                Llm.Response.Success.ReplyFinished,
            ),
            responses,
        )
        assertEquals(1, llm.prompts.size)
        assertEquals((DEFAULT_PROMPT + article).trimIndent(), llm.prompts.single().value)
    }

    @Test
    fun `WHEN content is readable at max length and llm is available THEN summarizer can summarize`() = runTest {
        val summarizer = summarizerProvider(FakeLlm()).getOrThrow()

        val canSummarize = summarizer.canSummarize(
            ContentProperties(
                contentLength = MAX_CONTENT_LENGTH,
                isReaderable = true,
            ),
        )

        assertTrue(canSummarize)
    }

    @Test
    fun `WHEN content is too long THEN summarizer cannot summarize`() = runTest {
        val summarizer = summarizerProvider(FakeLlm()).getOrThrow()

        val canSummarize = summarizer.canSummarize(
            ContentProperties(
                contentLength = MAX_CONTENT_LENGTH + 1,
                isReaderable = true,
            ),
        )

        assertFalse(canSummarize)
    }

    @Test
    fun `WHEN content is not readable THEN summarizer cannot summarize`() = runTest {
        val summarizer = summarizerProvider(FakeLlm()).getOrThrow()

        val canSummarize = summarizer.canSummarize(
            ContentProperties(
                contentLength = MAX_CONTENT_LENGTH,
                isReaderable = false,
            ),
        )

        assertFalse(canSummarize)
    }

    @Test
    fun `WHEN llm is unavailable THEN summarizer cannot summarize`() = runTest {
        val llm = FakeLlm(
            scriptedStatuses = listOf(
                Llm.Status.Available,
                Llm.Status.Unavailable,
            ),
        )
        val summarizer = summarizerProvider(llm).getOrThrow()

        val canSummarize = summarizer.canSummarize(
            ContentProperties(
                contentLength = MAX_CONTENT_LENGTH,
                isReaderable = true,
            ),
        )

        assertFalse(canSummarize)
    }
}

private class FakeLlm(
    private val scriptedResponses: List<Llm.Response> = listOf(
        Llm.Response.Success.ReplyPart("fake reply"),
        Llm.Response.Success.ReplyFinished,
    ),
    private val scriptedStatuses: List<Llm.Status> = listOf(Llm.Status.Available),
) : Llm {
    val prompts = mutableListOf<Prompt>()
    private var statusIndex = 0

    override suspend fun prompt(prompt: Prompt): Flow<Llm.Response> {
        prompts += prompt
        return flowOf(*scriptedResponses.toTypedArray())
    }

    override suspend fun checkStatus(): Llm.Status {
        val index = statusIndex.coerceAtMost(scriptedStatuses.lastIndex)
        if (statusIndex < scriptedStatuses.lastIndex) {
            statusIndex += 1
        }
        return scriptedStatuses[index]
    }
}
