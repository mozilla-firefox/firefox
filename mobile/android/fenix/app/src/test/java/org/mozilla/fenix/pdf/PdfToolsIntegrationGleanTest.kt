/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.test.core.app.ApplicationProvider
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.PdfViewer
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

/** [PdfToolsIntegration] tests that use [FenixGleanTestRule]. */
@RunWith(RobolectricTestRunner::class)
class PdfToolsIntegrationGleanTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val testDispatcher = StandardTestDispatcher()

    private val tabId = "1"

    private val container = CoordinatorLayout(ApplicationProvider.getApplicationContext())

    private val tab = createTab(url = "https://mozilla.org", id = tabId)

    private fun storeOf(tab: TabSessionState) =
        BrowserStore(
            initialState = BrowserState(tabs = listOf(tab), selectedTabId = tab.id),
            middleware = EngineMiddleware.create(engine = mockk(), scope = TestScope()),
        )

    private val browserStore = storeOf(tab)

    private fun integration(store: BrowserStore = browserStore) =
        PdfToolsIntegration(
            container = container,
            browserStore = store,
            isAddressBarAtBottom = true,
            mainDispatcher = testDispatcher,
        )

    private fun integrationWith(engineSession: EngineSession) =
        integration(store = storeOf(tab.copy(engineState = EngineState(engineSession = engineSession))))

    private fun PdfToolsIntegration.addSignature() {
        handleSignClick()
        handleSignAddClick()
    }

    @Test
    fun `GIVEN download has not been activated THEN nothing is recorded`() {
        integration()

        assertNull(PdfViewer.downloadTapped.testGetValue())
    }

    @Test
    fun `WHEN download is activated THEN the interaction is recorded`() {
        integration().handleDownloadClick()

        assertNotNull(PdfViewer.downloadTapped.testGetValue())
    }

    @Test
    fun `GIVEN print has not been activated THEN nothing is recorded`() {
        integration()

        assertNull(PdfViewer.printTapped.testGetValue())
    }

    @Test
    fun `WHEN print is activated THEN the interaction is recorded`() {
        integration().handlePrintClick()

        assertNotNull(PdfViewer.printTapped.testGetValue())
    }

    @Test
    fun `GIVEN share has not been activated THEN nothing is recorded`() {
        integration()

        assertNull(PdfViewer.shareTapped.testGetValue())
    }

    @Test
    fun `WHEN share is activated THEN the interaction is recorded`() {
        integration().handleShareClick()

        assertNotNull(PdfViewer.shareTapped.testGetValue())
    }

    @Test
    fun `WHEN the signature is cleared THEN the interaction is recorded`() {
        integration().handleSignClearClick()

        val event = assertNotNull(PdfViewer.signDialogClearTapped.testGetValue())
        assertEquals(SignatureType.Typed.telemetryName, event.single().extra!!["signature_type"])
    }

    @Test
    fun `WHEN the signature is added THEN the interaction is recorded`() {
        integration().addSignature()

        val event = assertNotNull(PdfViewer.signDialogAddTapped.testGetValue())
        assertEquals(SignatureType.Typed.telemetryName, event.single().extra!!["signature_type"])
    }

    @Test
    fun `WHEN the signature box is dismissed THEN the interaction is recorded`() {
        integration().handleSignCloseClick()

        val event = assertNotNull(PdfViewer.signDialogCloseTapped.testGetValue())
        assertEquals(SignatureType.Typed.telemetryName, event.single().extra!!["signature_type"])
    }

    @Test
    fun `WHEN the signature reaches the PDF viewer THEN the success is recorded`() {
        val engineSession = mockk<EngineSession>()
        val onResult = slot<() -> Unit>()
        every { engineSession.addSignatureToPdf(any(), capture(onResult), any()) } returns Unit
        integrationWith(engineSession).addSignature()

        onResult.captured()

        val event = assertNotNull(PdfViewer.signDialogAddCompleted.testGetValue())
        assertEquals(SignatureType.Typed.telemetryName, event.single().extra!!["signature_type"])
        assertNull(PdfViewer.signDialogAddFailure.testGetValue())
    }

    @Test
    fun `WHEN the signature cannot reach the PDF viewer THEN the failure is recorded`() {
        val engineSession = mockk<EngineSession>()
        val onException = slot<(Throwable) -> Unit>()
        every { engineSession.addSignatureToPdf(any(), any(), capture(onException)) } returns Unit
        integrationWith(engineSession).addSignature()

        onException.captured(RuntimeException())

        val extra = assertNotNull(PdfViewer.signDialogAddFailure.testGetValue()).single().extra!!
        assertEquals(SignatureFailure.EngineError.telemetryName, extra["reason"])
        assertEquals(SignatureType.Typed.telemetryName, extra["signature_type"])
        assertNull(PdfViewer.signDialogAddCompleted.testGetValue())
    }

    @Test
    fun `GIVEN the PDF viewer does not respond WHEN the signature is added THEN the timeout is recorded`() {
        val engineSession = mockk<EngineSession>()
        every { engineSession.addSignatureToPdf(any(), any(), any()) } returns Unit
        val integration = integrationWith(engineSession)
        integration.start()
        integration.addSignature()

        testDispatcher.scheduler.advanceUntilIdle()

        val extra = assertNotNull(PdfViewer.signDialogAddFailure.testGetValue()).single().extra!!
        assertEquals(SignatureFailure.Timeout.telemetryName, extra["reason"])
    }

    @Test
    fun `GIVEN the selected tab has no engine session WHEN the signature is added THEN the failure is recorded`() {
        integration().addSignature()

        val extra = assertNotNull(PdfViewer.signDialogAddFailure.testGetValue()).single().extra!!
        assertEquals(SignatureFailure.NoEngineSession.telemetryName, extra["reason"])
        assertEquals(SignatureType.Typed.telemetryName, extra["signature_type"])
    }

    @Test
    fun `WHEN sign is activated THEN the interaction is recorded`() {
        integration().handleSignClick()

        assertNotNull(PdfViewer.signTapped.testGetValue())
    }
}
