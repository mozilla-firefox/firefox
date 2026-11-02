/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import android.content.Context
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import androidx.activity.ComponentActivity
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.children
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import kotlin.test.assertIs
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.ext.components
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
class PdfToolsIntegrationTest {
    private val tabId = "1"

    private val activity = Robolectric.buildActivity(ComponentActivity::class.java).setup().get()

    private val container = CoordinatorLayout(activity)

    private val testDispatcher = StandardTestDispatcher()

    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

    // The tools are only shown on a PDF, so the tab is set up as one.
    private val pdfTab =
        createTab(url = "https://mozilla.org/document.pdf", id = tabId).let {
            it.copy(content = it.content.copy(isPdf = true))
        }

    private fun storeOf(tab: TabSessionState, customTabs: List<CustomTabSessionState> = emptyList()) =
        BrowserStore(
            initialState = BrowserState(tabs = listOf(tab), customTabs = customTabs, selectedTabId = tab.id),
            middleware =
                listOf(captureActionsMiddleware) + EngineMiddleware.create(engine = mockk(), scope = TestScope()),
        )

    private val browserStore = storeOf(pdfTab)

    private fun pdfTabWith(engineSession: EngineSession) =
        pdfTab.copy(engineState = EngineState(engineSession = engineSession))

    private val customTabId = "custom"

    private fun customPdfTabWith(engineSession: EngineSession? = null) =
        createCustomTab(url = "https://mozilla.org/custom.pdf", id = customTabId, engineSession = engineSession).let {
            it.copy(content = it.content.copy(isPdf = true))
        }

    // The selected tab is a PDF too, so the tools must tell the two apart.
    private fun storeWithCustomTab(customTab: CustomTabSessionState = customPdfTabWith()) =
        storeOf(pdfTab, customTabs = listOf(customTab))

    private fun integration(
        isAddressBarAtBottom: Boolean = true,
        store: BrowserStore = browserStore,
        customTabSessionId: String? = null,
    ) =
        PdfToolsIntegration(
            container = container,
            browserStore = store,
            isAddressBarAtBottom = isAddressBarAtBottom,
            customTabSessionId = customTabSessionId,
            mainDispatcher = testDispatcher,
        )

    @Before
    fun setUp() {
        every { testContext.components.appStore } returns AppStore()
        // The tools are added from a runnable posted on the container, which only runs once it is attached.
        activity.setContentView(container)
    }

    private val layoutParams: CoordinatorLayout.LayoutParams
        get() = container.getChildAt(0).layoutParams as CoordinatorLayout.LayoutParams

    @Test
    fun `WHEN the feature is started and stopped THEN the tools are added then removed`() {
        val integration = integration()

        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals(2, container.childCount)

        integration.stop()
        assertEquals(0, container.childCount)
    }

    @Test
    fun `WHEN the feature is restarted THEN the tools are added back to the container`() {
        val integration = integration()

        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        integration.stop()
        integration.start()
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(2, container.childCount)
    }

    @Test
    fun `WHEN the feature is started twice THEN only one set of tools is added`() {
        val integration = integration()

        integration.start()
        integration.start()
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(2, container.childCount)
    }

    @Test
    fun `WHEN the feature is started THEN the tools are positioned by their behavior`() {
        integration().start()
        shadowOf(Looper.getMainLooper()).idle()

        assertIs<PdfOverlayBehavior>(layoutParams.behavior)
    }

    @Test
    @Config(qualifiers = "sw800dp")
    fun `GIVEN a tablet window WHEN the feature is started THEN the tools and the dialog anchor to opposite edges`() {
        // Test for Bug 2067261
        integration(isAddressBarAtBottom = false).start()
        shadowOf(Looper.getMainLooper()).idle()

        val gravities =
            container.children.map { overlay ->
                val params = overlay.layoutParams as CoordinatorLayout.LayoutParams
                val behavior = params.behavior as PdfOverlayBehavior
                behavior.onLayoutChild(container, overlay, View.LAYOUT_DIRECTION_LTR)
                params.gravity
            }

        assertEquals(listOf(Gravity.TOP, Gravity.BOTTOM), gravities.toList())
    }

    @Test
    fun `WHEN a signature is added THEN the typed text is handed to the engine and the dialog closes`() {
        val engineSession = mockk<EngineSession>()
        val onResult = slot<() -> Unit>()
        every { engineSession.addSignatureToPdf(any(), capture(onResult), any()) } returns Unit
        val integration = integration(store = storeOf(pdfTabWith(engineSession)))
        integration.handleSignClick()
        integration.signatureState.signature.setTextAndPlaceCursorAtEnd("Test User")

        integration.handleSignAddClick()

        verify { engineSession.addSignatureToPdf(eq("Test User"), any(), any()) }
        assertTrue("The dialog stays up until the engine has the signature.", integration.signatureState.isSigning)

        onResult.captured()

        assertFalse(integration.signatureState.isSigning)
    }

    @Test
    fun `WHEN add is activated again before or after the engine responds THEN the signature is handed over once`() {
        val engineSession = mockk<EngineSession>()
        val onResult = slot<() -> Unit>()
        every { engineSession.addSignatureToPdf(any(), capture(onResult), any()) } returns Unit
        val integration = integration(store = storeOf(pdfTabWith(engineSession)))
        integration.handleSignClick()
        integration.signatureState.signature.setTextAndPlaceCursorAtEnd("Test User")

        integration.handleSignAddClick()
        integration.handleSignAddClick()
        onResult.captured()
        integration.handleSignAddClick()

        verify(exactly = 1) { engineSession.addSignatureToPdf(any(), any(), any()) }
    }

    @Test
    fun `GIVEN a signature is being added WHEN the dialog is closed and reopened THEN the signature is kept until the engine responds`() {
        val engineSession = mockk<EngineSession>()
        val onResult = slot<() -> Unit>()
        every { engineSession.addSignatureToPdf(any(), capture(onResult), any()) } returns Unit
        val integration = integration(store = storeOf(pdfTabWith(engineSession)))
        integration.handleSignClick()
        integration.signatureState.signature.setTextAndPlaceCursorAtEnd("Test User")
        integration.handleSignAddClick()

        integration.handleSignCloseClick()
        integration.handleSignClick()

        assertTrue(integration.signatureState.isAddingSignature)
        assertEquals("Test User", integration.signatureState.signature.text.toString())

        onResult.captured()

        assertFalse(integration.signatureState.isSigning)
        assertEquals("", integration.signatureState.signature.text.toString())
    }

    @Test
    fun `GIVEN an answered signature WHEN the next signature gets no response THEN only the next one times out`() {
        val engineSession = mockk<EngineSession>()
        val onResult = slot<() -> Unit>()
        every { engineSession.addSignatureToPdf(any(), capture(onResult), any()) } returns Unit
        val integration = integration(store = storeOf(pdfTabWith(engineSession)))
        integration.start()
        integration.handleSignClick()
        integration.handleSignAddClick()
        onResult.captured()
        integration.handleSignClick()
        integration.signatureState.signature.setTextAndPlaceCursorAtEnd("Test User")

        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue("An answered signature does not time out.", integration.signatureState.isSigning)

        integration.handleSignAddClick()
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse(integration.signatureState.isSigning)
        assertFalse(integration.signatureState.isAddingSignature)
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
        val response =
            Response(
                url = pdfTab.content.url,
                status = 200,
                headers = MutableHeaders(),
                body = Response.Body.empty(),
            )
        val engineSession = mockk<EngineSession>()
        every { engineSession.requestPdfToShare(any(), any()) } answers
            {
                firstArg<(Response) -> Unit>().invoke(response)
            }

        integration(store = storeOf(pdfTabWith(engineSession))).handleShareClick()

        captureActionsMiddleware.assertFirstAction(ShareResourceAction.AddShareAction::class) {
            assertEquals(tabId, it.tabId)
            assertIs<ShareResourceState.DirectResource>(it.resource)
        }
    }

    @Test
    fun `GIVEN a PDF opened from a local file WHEN share is activated THEN the local file is shared`() {
        val localTab =
            createTab(url = "content://downloads/document.pdf", id = tabId).let {
                it.copy(content = it.content.copy(isPdf = true))
            }

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

    @Test
    fun `GIVEN a signature is in progress WHEN another PDF becomes the selected tab THEN the signature is abandoned`() {
        val integration = integration()
        integration.handleSignClick()
        integration.signatureState.signature.setTextAndPlaceCursorAtEnd("Mark Johnson")

        integration.handlePdfTabChanged("2")

        assertFalse(integration.signatureState.isSigning)
        assertEquals("", integration.signatureState.signature.text.toString())
    }

    @Test
    fun `GIVEN a signature is in progress WHEN the selected tab stops showing a PDF THEN the signature is abandoned`() {
        val integration = integration()
        integration.handleSignClick()

        integration.handlePdfTabChanged(null)

        assertFalse(integration.signatureState.isSigning)
    }

    @Test
    fun `GIVEN a signature is in progress WHEN the same PDF stays selected THEN the signature is kept`() {
        val integration = integration()
        integration.handleSignClick()

        integration.handlePdfTabChanged(tabId)

        assertTrue(integration.signatureState.isSigning)
    }

    @Test
    fun `GIVEN the feature is started WHEN the selected tab leaves the PDF viewer THEN the signature is abandoned`() {
        val integration = integration()
        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        integration.handleSignClick()

        browserStore.dispatch(ContentAction.ExitedPdfViewer(tabId))
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse(integration.signatureState.isSigning)
    }

    @Test
    fun `GIVEN a custom tab WHEN download, print, and share are activated THEN each acts on the custom tab`() {
        val integration = integration(store = storeWithCustomTab(), customTabSessionId = customTabId)

        integration.handleDownloadClick()
        integration.handlePrintClick()
        integration.handleShareClick()

        captureActionsMiddleware.assertFirstAction(EngineAction.SaveToPdfAction::class) {
            assertEquals(customTabId, it.tabId)
        }
        captureActionsMiddleware.assertFirstAction(EngineAction.PrintContentAction::class) {
            assertEquals(customTabId, it.tabId)
        }
        captureActionsMiddleware.assertFirstAction(ShareResourceAction.AddShareAction::class) {
            assertEquals(customTabId, it.tabId)
        }
    }

    @Test
    fun `GIVEN a custom tab WHEN a signature is added THEN the custom tab gets the signature`() {
        val engineSession = mockk<EngineSession>(relaxed = true)
        val integration =
            integration(store = storeWithCustomTab(customPdfTabWith(engineSession)), customTabSessionId = customTabId)
        integration.handleSignClick()
        integration.signatureState.signature.setTextAndPlaceCursorAtEnd("Test User")

        integration.handleSignAddClick()

        verify { engineSession.addSignatureToPdf(eq("Test User"), any(), any()) }
    }

    @Test
    fun `GIVEN a signature is in progress on a custom tab WHEN the selected tab and then the custom tab leave the PDF viewer THEN only the custom tab abandons it`() {
        val store = storeWithCustomTab()
        val integration = integration(store = store, customTabSessionId = customTabId)
        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        integration.handleSignClick()

        store.dispatch(ContentAction.ExitedPdfViewer(tabId))
        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue("The selected tab leaving does not affect the custom tab.", integration.signatureState.isSigning)

        store.dispatch(ContentAction.ExitedPdfViewer(customTabId))
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse(integration.signatureState.isSigning)
    }

    @Test
    fun `GIVEN the address bar is at the top WHEN the feature is started THEN the overlays are traversed between the address bar and the page`() {
        // Test for Bug 2066197
        val addressBar = View(activity).apply { id = R.id.composable_toolbar }
        val content = View(activity).apply { id = R.id.engineView }
        container.addView(addressBar)
        container.addView(content)

        integration(isAddressBarAtBottom = false).start()
        shadowOf(Looper.getMainLooper()).idle()

        val tools = container.children.elementAt(2)
        val dialog = (container.children.elementAt(3) as ViewGroup).getChildAt(0)
        assertEquals(addressBar.id, tools.accessibilityTraversalAfter)
        assertEquals(tools.id, dialog.accessibilityTraversalAfter)
        assertEquals(dialog.id, content.accessibilityTraversalAfter)
    }

    @Test
    fun `GIVEN the address bar is at the bottom WHEN the feature is started THEN the overlays are traversed between the page and the address bar`() {
        // Test for Bug 2085012
        val addressBar = View(activity).apply { id = R.id.composable_toolbar }
        val content = View(activity).apply { id = R.id.engineView }
        container.addView(addressBar)
        container.addView(content)

        integration(isAddressBarAtBottom = true).start()
        shadowOf(Looper.getMainLooper()).idle()

        val tools = container.children.elementAt(2)
        val dialog = container.children.elementAt(3)
        assertEquals(content.id, tools.accessibilityTraversalAfter)
        assertEquals(tools.id, dialog.accessibilityTraversalAfter)
        assertEquals(dialog.id, addressBar.accessibilityTraversalAfter)
    }

    /** Stands in for the browser toolbar, which removes the navigation bar as its composition is created. */
    private class SiblingRemovingView(context: Context, private val sibling: View) : View(context) {
        override fun onAttachedToWindow() {
            super.onAttachedToWindow()
            (sibling.parent as? ViewGroup)?.removeView(sibling)
        }
    }

    @Test
    fun `GIVEN chrome that removes a sibling while attaching WHEN the feature is started THEN the container attaches`() {
        // Test for Bug 2065098
        val detachedContainer = CoordinatorLayout(activity)
        val navigationBar = View(activity).apply { id = R.id.navigation_bar }
        detachedContainer.addView(navigationBar)
        detachedContainer.addView(SiblingRemovingView(activity, navigationBar).apply { id = R.id.composable_toolbar })

        PdfToolsIntegration(
                container = detachedContainer,
                browserStore = browserStore,
                isAddressBarAtBottom = true,
            )
            .start()

        activity.setContentView(detachedContainer)
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals(3, detachedContainer.childCount)
    }
}
