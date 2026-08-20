/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.test.core.app.ApplicationProvider
import io.mockk.mockk
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
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

    private val tabId = "1"

    private val container = CoordinatorLayout(ApplicationProvider.getApplicationContext())

    private val browserStore =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs = listOf(createTab(url = "https://mozilla.org", id = tabId)),
                    selectedTabId = tabId,
                ),
            middleware = EngineMiddleware.create(engine = mockk(), scope = TestScope()),
        )

    private fun integration() =
        PdfToolsIntegration(
            container = container,
            browserStore = browserStore,
            isAddressBarAtBottom = true,
        )

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
}
