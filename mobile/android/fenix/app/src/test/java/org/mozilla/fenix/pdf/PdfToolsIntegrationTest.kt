/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.test.core.app.ApplicationProvider
import io.mockk.mockk
import kotlin.test.assertIs
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class PdfToolsIntegrationTest {
    private val tabId = "1"

    private val container = CoordinatorLayout(ApplicationProvider.getApplicationContext())

    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

    // The tools are only shown on a PDF, so the tab is set up as one.
    private val pdfTab =
        createTab(url = "https://mozilla.org/document.pdf", id = tabId).let {
            it.copy(content = it.content.copy(isPdf = true))
        }

    private fun storeOf(tab: TabSessionState) =
        BrowserStore(
            initialState = BrowserState(tabs = listOf(tab), selectedTabId = tab.id),
            middleware =
                listOf(captureActionsMiddleware) + EngineMiddleware.create(engine = mockk(), scope = TestScope()),
        )

    private val browserStore = storeOf(pdfTab)

    private fun integration(
        isAddressBarAtBottom: Boolean = true,
        store: BrowserStore = browserStore,
    ) =
        PdfToolsIntegration(
            container = container,
            browserStore = store,
            isAddressBarAtBottom = isAddressBarAtBottom,
        )

    private val layoutParams: CoordinatorLayout.LayoutParams
        get() = container.getChildAt(0).layoutParams as CoordinatorLayout.LayoutParams

    @Test
    fun `WHEN the feature is started and stopped THEN the tools are added then removed`() {
        val integration = integration()

        integration.start()
        assertEquals(1, container.childCount)

        integration.stop()
        assertEquals(0, container.childCount)
    }

    @Test
    fun `WHEN the feature is restarted THEN the tools are added back to the container`() {
        val integration = integration()

        integration.start()
        integration.stop()
        integration.start()

        assertEquals(1, container.childCount)
    }

    @Test
    fun `WHEN the feature is started twice THEN only one set of tools is added`() {
        val integration = integration()

        integration.start()
        integration.start()

        assertEquals(1, container.childCount)
    }

    @Test
    fun `WHEN the feature is started THEN the tools are positioned by their behavior`() {
        integration().start()

        assertIs<PdfToolsBehavior>(layoutParams.behavior)
    }

    @Test
    fun `WHEN download is activated THEN the selected tab is saved as a PDF`() {
        integration().handleDownloadClick()

        captureActionsMiddleware.assertFirstAction(EngineAction.SaveToPdfAction::class) {
            assertEquals(tabId, it.tabId)
        }
    }

    @Test
    fun `WHEN print is activated THEN the selected tab is printed`() {
        integration().handlePrintClick()

        captureActionsMiddleware.assertFirstAction(EngineAction.PrintContentAction::class) {
            assertEquals(tabId, it.tabId)
        }
    }

    @Test
    fun `WHEN share is activated THEN the selected tab's PDF is shared`() {
        integration().handleShareClick()

        captureActionsMiddleware.assertFirstAction(ShareResourceAction.AddShareAction::class) {
            assertEquals(tabId, it.tabId)
            assertIs<ShareResourceState.InternetResource>(it.resource)
        }
    }

    @Test
    fun `GIVEN a PDF opened from a local file WHEN share is activated THEN the local file is shared`() {
        val localTab = createTab(url = "content://downloads/document.pdf", id = tabId)

        integration(store = storeOf(localTab)).handleShareClick()

        captureActionsMiddleware.assertFirstAction(ShareResourceAction.AddShareAction::class) {
            assertEquals(tabId, it.tabId)
            assertIs<ShareResourceState.LocalResource>(it.resource)
        }
    }

    @Test
    fun `GIVEN the selected tab is not a PDF WHEN share is activated THEN nothing is shared`() {
        browserStore.dispatch(ContentAction.ExitedPdfViewer(tabId))

        integration().handleShareClick()

        captureActionsMiddleware.assertNotDispatched(ShareResourceAction.AddShareAction::class)
    }
}
