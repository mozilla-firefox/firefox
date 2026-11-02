/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import android.view.View
import android.view.ViewGroup
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.FrameLayout
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.clearText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.ComposeView
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.children
import androidx.core.view.isVisible
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.ktx.android.view.ImeInsetsSynchronizer
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.PdfViewer
import org.mozilla.fenix.R
import org.mozilla.fenix.components.share.tryDispatchPdfShareAction
import org.mozilla.fenix.pdf.ui.PdfTools
import org.mozilla.fenix.pdf.ui.PdfToolsContent
import org.mozilla.fenix.pdf.ui.SignatureDialogContent
import org.mozilla.fenix.theme.FirefoxTheme

private const val ADDING_SIGNATURE_TIMEOUT_MS = 5000L

/**
 * This integration is responsible for adding or removing PDF tools and properly anchoring it to the browser. [PdfTools]
 * only show when a PDF is displayed on the browser.
 *
 * @param container The containing browser [CoordinatorLayout] to add the PDF tools onto.
 * @param browserStore The [BrowserStore] to observe the PDF status of the current session.
 * @param isAddressBarAtBottom Whether the address bar is at the bottom of the browser.
 * @param customTabSessionId Optional custom tab session ID, or null to use the selected session.
 * @param mainDispatcher The [CoroutineDispatcher] the PDF status of the selected tab is observed on.
 */
class PdfToolsIntegration(
    private val container: CoordinatorLayout,
    private val browserStore: BrowserStore,
    private val isAddressBarAtBottom: Boolean,
    private val customTabSessionId: String? = null,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {

    private var overlays = emptyList<View>()
    private var scope: CoroutineScope? = null
    private var pdfTabId by mutableStateOf(browserStore.state.findPdfTabId(customTabSessionId))
    private var isSigning by mutableStateOf(false)
    private var isAddingSignature by mutableStateOf(false)
    private var isAddingSignatureTimeout: Job? = null
    private val signature = TextFieldState()

    private val currentSession
        get() = browserStore.state.findCustomTabOrSelectedTab(customTabSessionId)

    internal val signatureState: SignatureState
        get() =
            SignatureState(
                isSigning = isSigning,
                isAddingSignature = isAddingSignature,
                signature = signature,
            )

    internal val toolActions =
        PdfToolActions(
            onSignClick = ::handleSignClick,
            onDownloadClick = ::handleDownloadClick,
            onPrintClick = ::handlePrintClick,
            onShareClick = ::handleShareClick,
        )

    internal val signatureActions =
        SignatureActions(
            onClearClick = ::handleSignClearClick,
            onAddClick = ::handleSignAddClick,
            onCloseClick = ::handleSignCloseClick,
        )

    override fun start() {
        if (overlays.isNotEmpty()) {
            return
        }

        // Keep track of the PDF's tab ID.
        handlePdfTabChanged(browserStore.state.findPdfTabId(customTabSessionId))
        scope = CoroutineScope(mainDispatcher)
        scope?.launch {
            browserStore.stateFlow
                .map { it.findPdfTabId(customTabSessionId) }
                .distinctUntilChanged()
                .collect {
                    handlePdfTabChanged(it)
                }
        }

        val tools =
            createOverlay(pdfToolsBehavior(isAddressBarAtBottom)) {
                val isLargeWindow = AcornWindowSize.isLargeWindow()

                PdfToolsContent(
                    isPdfShowing = pdfTabId != null,
                    isLargeWindow = isLargeWindow,
                    isCoveredBySignatureDialog = isSigning && !isLargeWindow,
                    toolActions = toolActions,
                )
            }
        val dialog =
            createOverlay(signatureDialogBehavior(isAddressBarAtBottom)) {
                SignatureDialogContent(
                    isPdfShowing = pdfTabId != null,
                    signatureState = signatureState,
                    signatureActions = signatureActions,
                )
            }

        val created = listOf(tools, moveWithKeyboard(dialog))
        overlays = created

        // Add once the container has attached, since the chrome removes a sibling during that pass (Bug 2065098).
        container.post {
            if (overlays === created) {
                created.forEach(container::addView)
                orderForAccessibility(tools, dialog)
            }
        }
    }

    override fun stop() {
        scope?.cancel()
        scope = null

        container.pageContent?.accessibilityTraversalAfter = View.NO_ID
        container.addressBar?.accessibilityTraversalAfter = View.NO_ID
        overlays.forEach(container::removeView)
        overlays = emptyList()
    }

    /**
     * Creates a view drawn over the browser.
     *
     * @param overlayBehavior How the overlay should position itself in relationship to the browser.
     * @param content The content for the overlay.
     */
    private fun createOverlay(overlayBehavior: PdfOverlayBehavior, content: @Composable () -> Unit) =
        ComposeView(container.context).apply {
            id = View.generateViewId()
            layoutParams =
                CoordinatorLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { behavior = overlayBehavior }

            setContent { FirefoxTheme(content = content) }
        }

    /**
     * Moves the view smoothly with the keyboard.
     *
     * @param view An overlay anchored to the bottom of the browser.
     * @return The view to add to the browser, which is the overlay itself when the browser moves it already.
     */
    private fun moveWithKeyboard(view: ComposeView): View {
        if (isAddressBarAtBottom) {
            return view
        }

        val holder =
            FrameLayout(container.context).apply {
                layoutParams = view.layoutParams
                addView(view, FrameLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            }

        ImeInsetsSynchronizer.setup(
            targetView = view,
            insetsSource = holder,
            onIMEAnimationStarted = { isKeyboardShowingUp, keyboardHeight ->
                if (!isKeyboardShowingUp) {
                    (view.layoutParams as? ViewGroup.MarginLayoutParams)?.bottomMargin = keyboardHeight
                    view.requestLayout()
                }
            },
            onIMEAnimationFinished = { isKeyboardShowingUp, keyboardHeight ->
                if (isKeyboardShowingUp || keyboardHeight == 0) {
                    (view.layoutParams as? ViewGroup.MarginLayoutParams)?.bottomMargin = 0
                    view.requestLayout()
                }
            },
        )
        return holder
    }

    /**
     * Ensures the screenreader focus order shows the PDF tools between the address bar and the content.
     *
     * @param tools The PDF tools overlay.
     * @param dialog The signature dialog overlay.
     */
    private fun orderForAccessibility(tools: View, dialog: View) {
        val addressBar = container.addressBar ?: return
        val pageContent = container.pageContent ?: return

        if (isAddressBarAtBottom) {
            tools.accessibilityTraversalAfter = pageContent.id
            dialog.accessibilityTraversalAfter = tools.id
            addressBar.accessibilityTraversalAfter = dialog.id
        } else {
            tools.accessibilityTraversalAfter = addressBar.id
            dialog.accessibilityTraversalAfter = tools.id
            pageContent.accessibilityTraversalAfter = dialog.id
        }
    }

    /** Opens the dialog for adding a signature to the PDF. */
    internal fun handleSignClick() {
        isSigning = true
        PdfViewer.signTapped.record(NoExtras())
    }

    /** Erases the signature that was typed. */
    internal fun handleSignClearClick() {
        signature.clearText()
        PdfViewer.signDialogClearTapped.record(
            PdfViewer.SignDialogClearTappedExtra(signatureType = SignatureType.Typed.telemetryName)
        )
    }

    /** Adds the typed signature to the PDF and closes the dialog. */
    internal fun handleSignAddClick() {
        if (!isSigning || isAddingSignature) {
            return
        }

        PdfViewer.signDialogAddTapped.record(
            PdfViewer.SignDialogAddTappedExtra(signatureType = SignatureType.Typed.telemetryName)
        )

        val engineSession = currentSession?.engineState?.engineSession
        if (engineSession == null) {
            PdfViewer.signDialogAddFailure.record(
                PdfViewer.SignDialogAddFailureExtra(
                    reason = SignatureFailure.NoEngineSession.telemetryName,
                    signatureType = SignatureType.Typed.telemetryName,
                )
            )
            dismissSignatureDialog()
            return
        }

        isAddingSignature = true

        // Timeout to ensure [engineSession.addSignatureToPdf] completes in a timely way.
        // If it doesn't complete within [ADDING_SIGNATURE_TIMEOUT_MS], it is likely something on the backend failed
        // without reporting.
        isAddingSignatureTimeout = scope?.launch {
            delay(ADDING_SIGNATURE_TIMEOUT_MS)

            PdfViewer.signDialogAddFailure.record(
                PdfViewer.SignDialogAddFailureExtra(
                    reason = SignatureFailure.Timeout.telemetryName,
                    signatureType = SignatureType.Typed.telemetryName,
                )
            )
            dismissSignatureDialog()
        }
        engineSession.addSignatureToPdf(
            text = signature.text.toString(),
            onResult = {
                PdfViewer.signDialogAddCompleted.record(
                    PdfViewer.SignDialogAddCompletedExtra(signatureType = SignatureType.Typed.telemetryName)
                )
                if (isAddingSignature) {
                    dismissSignatureDialog()
                }
            },
            onException = {
                PdfViewer.signDialogAddFailure.record(
                    PdfViewer.SignDialogAddFailureExtra(
                        reason = SignatureFailure.EngineError.telemetryName,
                        signatureType = SignatureType.Typed.telemetryName,
                    )
                )
                if (isAddingSignature) {
                    dismissSignatureDialog()
                }
            },
        )
    }

    /** Closes the dialog. If the signature is processing, it will keep the signature state. */
    internal fun handleSignCloseClick() {
        if (isAddingSignature) {
            isSigning = false
        } else {
            dismissSignatureDialog()
        }
        PdfViewer.signDialogCloseTapped.record(
            PdfViewer.SignDialogCloseTappedExtra(signatureType = SignatureType.Typed.telemetryName)
        )
    }

    /** Clears out the state if the user navigates away. */
    internal fun handlePdfTabChanged(tabId: String?) {
        if (pdfTabId != null && tabId != pdfTabId) {
            dismissSignatureDialog()
        }
        pdfTabId = tabId
    }

    private fun dismissSignatureDialog() {
        isAddingSignatureTimeout?.cancel()
        isAddingSignatureTimeout = null
        isSigning = false
        isAddingSignature = false
        signature.clearText()
    }

    /** Saves the PDF the selected tab is displaying to the device. */
    internal fun handleDownloadClick() {
        PdfViewer.downloadTapped.record(NoExtras())
        currentSession?.id?.let {
            browserStore.dispatch(EngineAction.SaveToPdfAction(it))
        }
    }

    /** Prints the PDF the selected tab is displaying. */
    internal fun handlePrintClick() {
        PdfViewer.printTapped.record(NoExtras())
        currentSession?.id?.let {
            browserStore.dispatch(EngineAction.PrintContentAction(it))
        }
    }

    /** Shares the PDF the selected tab is displaying. */
    internal fun handleShareClick() {
        PdfViewer.shareTapped.record(NoExtras())
        val tab = currentSession ?: return
        browserStore.tryDispatchPdfShareAction(tabId = tab.id, url = tab.content.url)
    }
}

private val CoordinatorLayout.addressBar: View?
    get() = children.firstOrNull { it.isVisible && it.id in listOf(R.id.toolbar, R.id.composable_toolbar) }

private val CoordinatorLayout.pageContent: View?
    get() = findViewById(R.id.engineView)

private fun BrowserState.findPdfTabId(customTabSessionId: String?): String? =
    findCustomTabOrSelectedTab(customTabSessionId)?.takeIf { it.content.isPdf }?.id
