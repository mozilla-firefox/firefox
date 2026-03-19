/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import io.mockk.every
import io.mockk.mockk
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.feature.summarize.LlmAction
import mozilla.components.feature.summarize.LlmProviderAction
import mozilla.components.feature.summarize.ShakeConsentRequested
import mozilla.components.feature.summarize.SummarizationAction
import mozilla.components.feature.summarize.SummarizationFailed
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.ViewAppeared
import mozilla.components.feature.summarize.ViewDismissed
import mozilla.components.feature.summarize.content.PageMetadata
import mozilla.components.lib.state.Store
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.AiSummarize
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SummarizationTelemetryMiddlewareTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var middleware: SummarizationTelemetryMiddleware

    private val store =
        mockk<Store<SummarizationState, SummarizationAction>>(relaxed = true)

    @Before
    fun setup() {
        middleware = SummarizationTelemetryMiddleware(ConnectionType.WIFI)
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
    }

    @Test
    fun `WHEN ViewAppeared action is dispatched THEN summarization_requested is recorded`() {
        assertNull(AiSummarize.summarizationRequested.testGetValue())

        invokeMiddleware(ViewAppeared)

        assertNotNull(AiSummarize.summarizationRequested.testGetValue())
    }

    @Test
    fun `GIVEN user triggered via shake WHEN ViewAppeared is dispatched THEN trigger is set to SHAKE`() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = true)

        invokeMiddleware(ViewAppeared)
        invokeMiddleware(createLlmPromptedAction())

        val extras = AiSummarize.summarizationStarted.testGetValue()!!.first().extra!!
        assertEquals("SHAKE", extras["trigger"])
    }

    @Test
    fun `GIVEN user triggered via menu WHEN ViewAppeared is dispatched THEN trigger is set to MENU`() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)

        invokeMiddleware(ViewAppeared)
        invokeMiddleware(createLlmPromptedAction())

        val extras = AiSummarize.summarizationStarted.testGetValue()!!.first().extra!!
        assertEquals("MENU", extras["trigger"])
    }

    @Test
    fun `WHEN LlmPrompted action is dispatched THEN summarization_started is recorded with extras`() {
        assertNull(AiSummarize.summarizationStarted.testGetValue())

        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewAppeared)
        invokeMiddleware(
            LlmAction.SummarizationRequested(LlmProvider.Info(nameRes = 42)),
        )
        invokeMiddleware(
            createLlmPromptedAction(
                content = "hello world foo",
                pageMetadata = PageMetadata(
                    structuredDataTypes = listOf("recipe"),
                    language = "en",
                ),
            ),
        )

        val snapshot = AiSummarize.summarizationStarted.testGetValue()!!
        assertEquals(1, snapshot.size)

        val extras = snapshot.first().extra!!
        assertEquals("MENU", extras["trigger"])
        assertEquals("42", extras["model"])
        assertEquals("3", extras["length_words"])
        assertEquals("15", extras["length_chars"])
        assertEquals("[recipe]", extras["content_type"])
    }

    @Test
    fun `WHEN Llm Reply is received THEN summarization_completed is recorded with outcome true`() {
        assertNull(AiSummarize.summarizationCompleted.testGetValue())

        setupFullSession()
        invokeMiddleware(
            LlmAction.ReceivedResponse(Llm.Response.Success.ReplyFinished),
        )

        val snapshot = AiSummarize.summarizationCompleted.testGetValue()!!
        assertEquals(1, snapshot.size)

        val extras = snapshot.first().extra!!
        assertEquals("true", extras["outcome"])
        assertEquals("WIFI", extras["connection_type"])
        assertEquals("42", extras["model"])
        assertNull(extras["error_type"])
        assertNotNull(extras["time"])
    }

    @Test
    fun `WHEN Failure response is received THEN summarization_completed and summary_failure are recorded`() {
        assertNull(AiSummarize.summarizationCompleted.testGetValue())
        assertNull(AiSummarize.summaryFailure.testGetValue())

        setupFullSession()
        invokeMiddleware(
            LlmAction.ReceivedResponse(Llm.Response.Failure("server_error")),
        )

        val snapshot = AiSummarize.summarizationCompleted.testGetValue()!!
        assertEquals(1, snapshot.size)

        val extras = snapshot.first().extra!!
        assertEquals("false", extras["outcome"])
        assertEquals("server_error", extras["error_type"])

        assertNotNull(AiSummarize.summaryFailure.testGetValue())
    }

    @Test
    fun `WHEN SummarizationFailed is dispatched THEN summary_failure is recorded`() {
        assertNull(AiSummarize.summaryFailure.testGetValue())

        invokeMiddleware(SummarizationFailed(RuntimeException("extraction failed")))

        assertNotNull(AiSummarize.summaryFailure.testGetValue())
    }

    @Test
    fun `WHEN ProviderFailed is dispatched THEN summary_failure is recorded`() {
        assertNull(AiSummarize.summaryFailure.testGetValue())

        invokeMiddleware(LlmProviderAction.ProviderFailed)

        assertNotNull(AiSummarize.summaryFailure.testGetValue())
    }

    @Test
    fun `WHEN ProviderUnavailable is dispatched THEN summary_failure is recorded`() {
        assertNull(AiSummarize.summaryFailure.testGetValue())

        invokeMiddleware(LlmProviderAction.ProviderUnavailable)

        assertNotNull(AiSummarize.summaryFailure.testGetValue())
    }

    @Test
    fun `WHEN ViewDismissed is dispatched THEN summarization_closed is recorded`() {
        assertNull(AiSummarize.summarizationClosed.testGetValue())

        invokeMiddleware(ViewDismissed)

        assertNotNull(AiSummarize.summarizationClosed.testGetValue())
    }

    @Test
    fun `WHEN ViewDismissed is dispatched after session THEN model extra is included`() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewAppeared)
        invokeMiddleware(
            LlmAction.SummarizationRequested(LlmProvider.Info(nameRes = 99)),
        )
        invokeMiddleware(ViewDismissed)

        val extras = AiSummarize.summarizationClosed.testGetValue()!!.first().extra!!
        assertEquals("99", extras["model"])
    }

    @Test
    fun `WHEN ShakeConsentRequested is dispatched THEN summarization_consent_displayed is recorded`() {
        assertNull(AiSummarize.summarizationConsentDisplayed.testGetValue())

        invokeMiddleware(ShakeConsentRequested)

        assertNotNull(AiSummarize.summarizationConsentDisplayed.testGetValue())
    }

    @Test
    fun `GIVEN cellular connection WHEN summarization completes THEN connection_type is CELLULAR`() {
        middleware = SummarizationTelemetryMiddleware(ConnectionType.CELLULAR)

        setupFullSession()
        invokeMiddleware(
            LlmAction.ReceivedResponse(Llm.Response.Success.ReplyFinished),
        )

        val extras = AiSummarize.summarizationCompleted.testGetValue()!!.first().extra!!
        assertEquals("CELLULAR", extras["connection_type"])
    }

    private fun setupFullSession() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewAppeared)
        invokeMiddleware(
            LlmAction.SummarizationRequested(LlmProvider.Info(nameRes = 42)),
        )
        invokeMiddleware(createLlmPromptedAction())
    }

    private fun createLlmPromptedAction(
        content: String = "test content",
        pageMetadata: PageMetadata? = null,
    ) = LlmAction.LlmPrompted(
        instructions = "summarize this",
        content = content,
        pageMetadata = pageMetadata,
        llm = mockk(relaxed = true),
    )

    private fun invokeMiddleware(action: SummarizationAction) {
        middleware(
            store = store,
            next = {},
            action = action,
        )
    }
}
