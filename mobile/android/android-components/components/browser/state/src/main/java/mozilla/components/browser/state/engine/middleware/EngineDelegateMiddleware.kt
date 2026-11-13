/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.ActionWithTab
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.action.lookupTabIn
import mozilla.components.browser.state.action.toBrowserAction
import mozilla.components.browser.state.selector.allTabs
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.concept.engine.EngineSession
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * [Middleware] responsible for delegating calls to the appropriate [EngineSession] instance for actions like
 * [EngineAction.LoadUrlAction].
 */
internal class EngineDelegateMiddleware(private val scope: CoroutineScope) : Middleware<BrowserState, BrowserAction> {
    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        when (action) {
            is EngineAction.LoadUrlAction -> loadUrl(store, action)
            is EngineAction.LoadDataAction -> loadData(store, action)
            is EngineAction.ReloadAction -> reload(store, action)
            is EngineAction.GoBackAction -> goBack(store, action)
            is EngineAction.GoForwardAction -> goForward(store, action)
            is EngineAction.GoToHistoryIndexAction -> goToHistoryIndex(store, action)
            is EngineAction.ToggleDesktopModeAction -> toggleDesktopMode(store, action)
            is EngineAction.ExitFullScreenModeAction -> exitFullScreen(store, action)
            is EngineAction.SaveToPdfAction -> saveToPdf(store, action)
            is EngineAction.PrintContentAction -> printContent(store, action)
            is EngineAction.ClearDataAction -> clearData(store, action)
            is EngineAction.PurgeHistoryAction -> purgeHistory(store.state)
            is EngineAction.FlushEngineSessionStateAction -> flushEngineSessionSate(store, action)
            is TranslationsAction.TranslateAction -> {
                next(action)
                translate(store, action)
            }
            is TranslationsAction.TranslateRestoreAction -> {
                next(action)
                translateRestoreOriginal(store, action)
            }
            is PictureInPictureAction.RequestAction -> {
                next(action)
                requestPictureInPicture(store, action)
            }
            else -> next(action)
        }
    }

    private fun loadUrl(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.LoadUrlAction,
    ) = scope.launch {
        val tab = store.state.findTabOrCustomTab(action.tabId) ?: return@launch
        val engineSession = tab.engineState.engineSession

        if (engineSession == null && tab.content.url == action.url) {
            // This tab does not have an engine session and we are asked to load the URL this
            // session is already pointing to. Creating an EngineSession will do exactly
            // that in the linking step. So let's do that. Otherwise we would load the URL
            // twice.
            store.dispatch(EngineAction.CreateEngineSessionAction(action.tabId, includeParent = action.includeParent))
            return@launch
        }

        val parentEngineSession =
            if (action.includeParent && tab is TabSessionState) {
                tab.parentId?.let { store.state.findTabOrCustomTab(it)?.engineState?.engineSession }
            } else {
                null
            }

        getEngineSessionOrDispatch(store, action)
            ?.loadUrl(
                url = action.url,
                parent = parentEngineSession,
                flags = action.flags,
                additionalHeaders = action.additionalHeaders,
                textDirectiveUserActivation = action.textDirectiveUserActivation,
            )
    }

    private fun loadData(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.LoadDataAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.loadData(action.data, action.mimeType, action.encoding)
    }

    private fun reload(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.ReloadAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.reload(action.flags)
    }

    private fun goBack(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.GoBackAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.goBack(action.userInteraction)
    }

    private fun goForward(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.GoForwardAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.goForward(action.userInteraction)
    }

    private fun goToHistoryIndex(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.GoToHistoryIndexAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.goToHistoryIndex(action.index)
    }

    private fun toggleDesktopMode(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.ToggleDesktopModeAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.toggleDesktopMode(action.enable, reload = true)
    }

    private fun exitFullScreen(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.ExitFullScreenModeAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.exitFullScreenMode()
    }

    private fun saveToPdf(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.SaveToPdfAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.requestPdfToDownload()
    }

    private fun printContent(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.PrintContentAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.requestPrintContent()
    }

    private fun translate(
        store: Store<BrowserState, BrowserAction>,
        action: TranslationsAction.TranslateAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)
            ?.requestTranslate(action.fromLanguage, action.toLanguage, action.options)
    }

    private fun translateRestoreOriginal(
        store: Store<BrowserState, BrowserAction>,
        action: TranslationsAction.TranslateRestoreAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.requestTranslationRestore()
    }

    private fun requestPictureInPicture(
        store: Store<BrowserState, BrowserAction>,
        action: PictureInPictureAction.RequestAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.requestPictureInPicture()
    }

    private fun clearData(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.ClearDataAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.clearData(action.data)
    }

    private fun purgeHistory(state: BrowserState) = scope.launch {
        state.allTabs
            .mapNotNull { tab -> tab.engineState.engineSession }
            .forEach { engineSession -> engineSession.purgeHistory() }
    }

    private fun flushEngineSessionSate(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.FlushEngineSessionStateAction,
    ) = scope.launch {
        getEngineSessionOrDispatch(store, action)?.flushSessionState()
    }
}

/**
 * Returns the [EngineSession] of the tab targeted by the provided action. If the tab does not have an engine session
 * yet a new one will be created by dispatching a [EngineAction.CreateEngineSessionAction]. The provided [action] will
 * be dispatched as a follow up once the [EngineSession] has been created and initialized.
 *
 * @param store a reference to the browser store.
 * @param action the action to dispatch in case the engine session still has to be created.
 */
private fun getEngineSessionOrDispatch(
    store: Store<BrowserState, BrowserAction>,
    action: ActionWithTab,
): EngineSession? {
    val tab = action.lookupTabIn(store) ?: return null

    val engineSession = tab.engineState.engineSession

    return if (engineSession == null) {
        store.dispatch(
            EngineAction.CreateEngineSessionAction(
                action.tabId,
                followupAction = action.toBrowserAction(),
            )
        )
        null
    } else {
        engineSession
    }
}
