/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.engine.EngineObserver
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.ktx.kotlin.isExtensionUrl

/** [Middleware] that handles side-effects of linking a session to an engine session. */
internal class LinkingMiddleware(private val scope: CoroutineScope) : Middleware<BrowserState, BrowserAction> {

    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        var engineObserver: Pair<String, EngineObserver>? = null
        when (action) {
            is TabListAction.AddTabAction -> {
                if (action.tab.engineState.engineSession != null && action.tab.engineState.engineObserver == null) {
                    engineObserver =
                        link(
                            store,
                            action.tab.engineState.engineSession,
                            action.tab,
                            skipLoading = true,
                            includeParent = false,
                        )
                }
            }
            is TabListAction.AddMultipleTabsAction -> {
                require(action.tabs.none { it.engineState.engineSession != null }) {
                    "AddMultipleTabsAction does not support tabs with engine sessions"
                }
            }
            is EngineAction.UnlinkEngineSessionAction -> {
                unlink(store, action)
            }
            else -> {
                // no-op
            }
        }

        next(action)

        when (action) {
            is EngineAction.LinkEngineSessionAction -> {
                store.state.findTabOrCustomTab(action.tabId)?.let { tab ->
                    engineObserver = link(store, action.engineSession, tab, action.skipLoading, action.includeParent)
                }
            }
            else -> {
                // no-op
            }
        }

        engineObserver?.let {
            store.dispatch(EngineAction.UpdateEngineSessionObserverAction(it.first, it.second))
            store.dispatch(EngineAction.UpdateEngineSessionInitializingAction(it.first, false))
        }
    }

    private fun link(
        store: Store<BrowserState, BrowserAction>,
        engineSession: EngineSession,
        tab: SessionState,
        skipLoading: Boolean = true,
        includeParent: Boolean,
    ): Pair<String, EngineObserver> {
        val observer = EngineObserver(tab.id, store, scope)
        engineSession.register(observer)

        if (skipLoading) {
            return Pair(tab.id, observer)
        }

        // Bypass loading of "about:home". This will still add "about:home" to the session history.
        val loadFlags =
            if (tab.content.url == ABOUT_HOME_URL) {
                tab.engineState.initialLoadFlags.withBypassLoadUriDelegate()
            } else {
                tab.engineState.initialLoadFlags
            }

        if (tab.content.url.isExtensionUrl()) {
            // The parent tab/session is used as a referrer which is not accurate
            // for extension pages. The extension page is not loaded by the parent
            // tab, but opened by an extension e.g. via browser.tabs.update.
            performLoadOnMainThread(engineSession, tab.content.url, loadFlags = loadFlags)
        } else {
            val parentEngineSession =
                if (includeParent && tab is TabSessionState) {
                    tab.parentId?.let { store.state.findTabOrCustomTab(it)?.engineState?.engineSession }
                } else {
                    null
                }

            performLoadOnMainThread(
                engineSession = engineSession,
                url = tab.content.url,
                parent = parentEngineSession,
                loadFlags = loadFlags,
                additionalHeaders = tab.engineState.initialAdditionalHeaders,
                originalInput = tab.originalInput,
                textDirectiveUserActivation = tab.engineState.initialTextDirectiveUserActivation,
            )
        }

        return Pair(tab.id, observer)
    }

    private fun EngineSession.LoadUrlFlags.withBypassLoadUriDelegate() =
        if (contains(EngineSession.LoadUrlFlags.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE)) {
            this
        } else {
            EngineSession.LoadUrlFlags.select(
                value,
                EngineSession.LoadUrlFlags.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE,
            )
        }

    private fun performLoadOnMainThread(
        engineSession: EngineSession,
        url: String,
        parent: EngineSession? = null,
        loadFlags: EngineSession.LoadUrlFlags,
        additionalHeaders: Map<String, String>? = null,
        originalInput: String? = null,
        textDirectiveUserActivation: Boolean = false,
    ) = scope.launch {
        engineSession.loadUrl(
            url = url,
            parent = parent,
            flags = loadFlags,
            additionalHeaders = additionalHeaders,
            originalInput = originalInput,
            textDirectiveUserActivation = textDirectiveUserActivation,
        )
    }

    private fun unlink(
        store: Store<BrowserState, BrowserAction>,
        action: EngineAction.UnlinkEngineSessionAction,
    ) {
        val tab = store.state.findTabOrCustomTab(action.tabId) ?: return

        tab.engineState.engineObserver?.let {
            tab.engineState.engineSession?.unregister(it)
        }
    }
}
