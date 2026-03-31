/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import io.mockk.every
import io.mockk.mockk
import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.feature.summarize.ContentExtracted
import mozilla.components.feature.summarize.OffDeviceSummarizationShakeConsentAction
import mozilla.components.feature.summarize.SummarizationAction
import mozilla.components.feature.summarize.SummarizationCompleted
import mozilla.components.feature.summarize.SummarizationFailed
import mozilla.components.feature.summarize.SummarizationRequested
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.ViewAppeared
import mozilla.components.feature.summarize.ViewDismissed
import mozilla.components.feature.summarize.content.Content
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
        assertNull(AiSummarize.requested.testGetValue())

        invokeMiddleware(ViewAppeared)

        assertNotNull(AiSummarize.requested.testGetValue())
    }

    @Test
    fun `GIVEN user triggered via shake WHEN ViewAppeared is dispatched THEN trigger is set to SHAKE`() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = true)

        invokeMiddleware(ViewAppeared)
        invokeMiddleware(createContentExtractedAction())

        val extras = AiSummarize.started.testGetValue()!!.first().extra!!
        assertEquals("SHAKE", extras["trigger"])
    }

    @Test
    fun `GIVEN user triggered via menu WHEN ViewAppeared is dispatched THEN trigger is set to MENU`() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)

        invokeMiddleware(ViewAppeared)
        invokeMiddleware(createContentExtractedAction())

        val extras = AiSummarize.started.testGetValue()!!.first().extra!!
        assertEquals("MENU", extras["trigger"])
    }

    @Test
    fun `WHEN ContentExtracted action is dispatched THEN summarization_started is recorded with extras`() {
        assertNull(AiSummarize.started.testGetValue())

        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewAppeared)
        invokeMiddleware(
            SummarizationRequested(LlmProvider.Info(nameRes = 42)),
        )
        invokeMiddleware(
            createContentExtractedAction(
                content = "hello world foo",
                pageMetadata = PageMetadata(
                    structuredDataTypes = listOf("recipe"),
                    wordCount = 120,
                    language = "en",
                ),
            ),
        )

        val snapshot = AiSummarize.started.testGetValue()!!
        assertEquals(1, snapshot.size)

        val extras = snapshot.first().extra!!
        assertEquals("MENU", extras["trigger"])
        assertEquals("42", extras["model"])
        assertEquals("120", extras["length_words"])
        assertEquals("15", extras["length_chars"])
        assertEquals("[recipe]", extras["content_type"])
    }

    @Test
    fun `WHEN SummarizationCompleted is received THEN summarization_completed is recorded with success true`() {
        assertNull(AiSummarize.completed.testGetValue())

        setupFullSession()
        invokeMiddleware(SummarizationCompleted)

        val snapshot = AiSummarize.completed.testGetValue()!!
        assertEquals(1, snapshot.size)

        val extras = snapshot.first().extra!!
        assertEquals("true", extras["success"])
        assertEquals("WIFI", extras["connection_type"])
        assertEquals("42", extras["model"])
        assertNull(extras["error_type"])
        assertNotNull(extras["summarize_duration_ms"])
    }

    @Test
    fun `WHEN SummarizationFailed is received THEN summarization_completed is recorded with success false`() {
        assertNull(AiSummarize.completed.testGetValue())

        setupFullSession()
        val exception = Llm.Exception("Error", ErrorCode(1001))
        invokeMiddleware(SummarizationFailed(exception))

        val snapshot = AiSummarize.completed.testGetValue()!!
        assertEquals(1, snapshot.size)

        val extras = snapshot.first().extra!!
        assertEquals("false", extras["success"])
        assertEquals(exception.errorCode.value.toString(), extras["error_type"])
    }

    @Test
    fun `WHEN ViewDismissed is dispatched THEN summarization_closed is recorded`() {
        assertNull(AiSummarize.closed.testGetValue())

        invokeMiddleware(ViewDismissed)

        assertNotNull(AiSummarize.closed.testGetValue())
    }

    @Test
    fun `WHEN ViewDismissed is dispatched after session THEN model extra is included`() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewAppeared)
        invokeMiddleware(
            SummarizationRequested(LlmProvider.Info(nameRes = 99)),
        )
        invokeMiddleware(ViewDismissed)

        val extras = AiSummarize.closed.testGetValue()!!.first().extra!!
        assertEquals("99", extras["model"])
    }

    @Test
    fun `WHEN AllowClicked is dispatched THEN summarization_consent_displayed is recorded with agreed true`() {
        assertNull(AiSummarize.consentDisplayed.testGetValue())

        invokeMiddleware(OffDeviceSummarizationShakeConsentAction.AllowClicked)

        val extras = AiSummarize.consentDisplayed.testGetValue()!!.first().extra!!
        assertEquals("true", extras["agreed"])
    }

    @Test
    fun `WHEN CancelClicked is dispatched THEN summarization_consent_displayed is recorded with agreed false`() {
        assertNull(AiSummarize.consentDisplayed.testGetValue())

        invokeMiddleware(OffDeviceSummarizationShakeConsentAction.CancelClicked)

        val extras = AiSummarize.consentDisplayed.testGetValue()!!.first().extra!!
        assertEquals("false", extras["agreed"])
    }

    @Test
    fun `WHEN ViewDismissed on consent screen THEN summarization_consent_displayed is recorded with agreed false`() {
        assertNull(AiSummarize.consentDisplayed.testGetValue())

        every { store.state } returns SummarizationState.ShakeConsentRequired
        invokeMiddleware(ViewDismissed)

        val extras = AiSummarize.consentDisplayed.testGetValue()!!.first().extra!!
        assertEquals("false", extras["agreed"])
    }

    @Test
    fun `WHEN ViewDismissed not on consent screen THEN summarization_consent_displayed is not recorded`() {
        assertNull(AiSummarize.consentDisplayed.testGetValue())

        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewDismissed)

        assertNull(AiSummarize.consentDisplayed.testGetValue())
    }

    @Test
    fun `WHEN summarization completes THEN summarization_time is recorded`() {
        assertNull(AiSummarize.duration.testGetValue())

        setupFullSession()
        invokeMiddleware(SummarizationCompleted)

        assertNotNull(AiSummarize.duration.testGetValue())
    }

    @Test
    fun `GIVEN cellular connection WHEN SummarizationCompleted THEN connection_type is CELLULAR`() {
        middleware = SummarizationTelemetryMiddleware(ConnectionType.CELLULAR)

        setupFullSession()
        invokeMiddleware(SummarizationCompleted)

        val extras = AiSummarize.completed.testGetValue()!!.first().extra!!
        assertEquals("CELLULAR", extras["connection_type"])
    }

    private fun setupFullSession() {
        every { store.state } returns SummarizationState.Inert(initializedWithShake = false)
        invokeMiddleware(ViewAppeared)
        invokeMiddleware(
            SummarizationRequested(LlmProvider.Info(nameRes = 42)),
        )
        invokeMiddleware(createContentExtractedAction())
    }

    private fun createContentExtractedAction(
        content: String = "test content",
        pageMetadata: PageMetadata = PageMetadata(),
    ) = ContentExtracted(Content(pageMetadata, content))

    private fun invokeMiddleware(action: SummarizationAction) {
        middleware(
            store = store,
            next = {},
            action = action,
        )
    }
}
