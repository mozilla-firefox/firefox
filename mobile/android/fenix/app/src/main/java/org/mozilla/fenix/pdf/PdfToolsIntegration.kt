/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.ComposeView
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.PdfViewer
import org.mozilla.fenix.components.share.createPdfShareAction
import org.mozilla.fenix.pdf.ui.PdfTools
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * This integration is responsible for adding or removing PDF tools and properly anchoring it to the browser. [PdfTools]
 * only show when a PDF is displayed on the browser.
 *
 * @param container The containing browser [CoordinatorLayout] to add the PDF tools onto.
 * @param browserStore The [BrowserStore] to observe the PDF status of the selected tab.
 * @param isAddressBarAtBottom Whether the address bar is at the bottom of the browser.
 */
class PdfToolsIntegration(
    private val container: CoordinatorLayout,
    private val browserStore: BrowserStore,
    private val isAddressBarAtBottom: Boolean,
) : LifecycleAwareFeature {

    private var pdfTools: ComposeView? = null

    override fun start() {
        if (pdfTools != null) {
            return
        }

        pdfTools =
            ComposeView(container.context).apply {
                // The tools are positioned by their behavior, which insets them from the browser chrome.
                layoutParams =
                    CoordinatorLayout.LayoutParams(
                            CoordinatorLayout.LayoutParams.MATCH_PARENT,
                            CoordinatorLayout.LayoutParams.WRAP_CONTENT,
                        )
                        .apply { behavior = PdfToolsBehavior(isAddressBarAtBottom = isAddressBarAtBottom) }

                setContent { PdfToolsHost() }
            }

        container.addView(pdfTools)
    }

    override fun stop() {
        container.removeView(pdfTools)
        pdfTools = null
    }

    /** Saves the PDF the selected tab is displaying to the device. */
    internal fun handleDownloadClick() {
        PdfViewer.downloadTapped.record(NoExtras())
        browserStore.state.selectedTabId?.let {
            browserStore.dispatch(EngineAction.SaveToPdfAction(it))
        }
    }

    /** Prints the PDF the selected tab is displaying. */
    internal fun handlePrintClick() {
        PdfViewer.printTapped.record(NoExtras())
        browserStore.state.selectedTabId?.let {
            browserStore.dispatch(EngineAction.PrintContentAction(it))
        }
    }

    /** Shares the PDF the selected tab is displaying. */
    internal fun handleShareClick() {
        PdfViewer.shareTapped.record(NoExtras())
        val tab = browserStore.state.selectedTab ?: return
        browserStore.createPdfShareAction(tabId = tab.id, url = tab.content.url)?.let {
            browserStore.dispatch(it)
        }
    }

    @Composable
    private fun PdfToolsHost() {
        FirefoxTheme {
            PdfToolsContent(
                browserStore = browserStore,
                isLargeWindow = AcornWindowSize.isLargeWindow(),
                onDownloadClick = ::handleDownloadClick,
                onPrintClick = ::handlePrintClick,
                onShareClick = ::handleShareClick,
            )
        }
    }
}

/**
 * [PdfTools] are only shown when the browser is on a PDF page.
 *
 * @param browserStore Used to observe the PDF status of the selected tab.
 * @param isLargeWindow Used to determine if the device should be treated as a tablet.
 * @param onDownloadClick Invoked when the user activates the download PDF button.
 * @param onPrintClick Invoked when the user activates the print PDF button.
 * @param onShareClick Invoked when the user activates the share PDF button.
 */
@Composable
internal fun PdfToolsContent(
    browserStore: BrowserStore,
    isLargeWindow: Boolean,
    onDownloadClick: () -> Unit,
    onPrintClick: () -> Unit,
    onShareClick: () -> Unit,
) {
    val isPdf by remember {
        browserStore.stateFlow.map { it.isSelectedTabPdf }.distinctUntilChanged()
    }
        .collectAsStateWithLifecycle(initialValue = browserStore.state.isSelectedTabPdf)

    if (isPdf) {
        PdfTools(
            isLargeWindow = isLargeWindow,
            // Bug 2054910
            onSignClick = {},
            onDownloadClick = onDownloadClick,
            onPrintClick = onPrintClick,
            onShareClick = onShareClick,
        )
    }
}

private val BrowserState.isSelectedTabPdf: Boolean
    get() = selectedTab?.content?.isPdf == true
