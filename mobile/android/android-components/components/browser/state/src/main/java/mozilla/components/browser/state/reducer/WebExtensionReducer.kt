/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.ActiveOptionsPage
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.WebExtensionState

internal object WebExtensionReducer {
    /**
     * [WebExtensionAction] Reducer function for modifying a specific [WebExtensionState] in both
     * [SessionState.extensionState] or [BrowserState.extensions].
     */
    @Suppress("LongMethod")
    fun reduce(state: BrowserState, action: WebExtensionAction): BrowserState {
        return when (action) {
            is WebExtensionAction.InstallWebExtensionAction -> {
                val existingExtension = state.extensions[action.extension.id]
                if (existingExtension == null) {
                    state.copy(extensions = state.extensions + (action.extension.id to action.extension))
                } else {
                    state.updateWebExtensionState(action.extension.id) {
                        // Keep existing browser and page actions in case we received them before the install action
                        action.extension.copy(browserAction = it.browserAction, pageAction = it.pageAction)
                    }
                }
            }
            is WebExtensionAction.UninstallWebExtensionAction -> {
                state.copy(
                    extensions = state.extensions - action.extensionId,
                    tabs = state.tabs.map { it.copy(extensionState = it.extensionState - action.extensionId) },
                )
            }
            is WebExtensionAction.UninstallAllWebExtensionsAction -> {
                state.copy(
                    extensions = emptyMap(),
                    tabs = state.tabs.map { it.copy(extensionState = emptyMap()) },
                )
            }
            is WebExtensionAction.UpdateWebExtensionEnabledAction -> {
                state.updateWebExtensionState(action.extensionId) {
                    it.copy(enabled = action.enabled)
                }
            }
            is WebExtensionAction.UpdateWebExtensionAllowedInPrivateBrowsingAction -> {
                state.updateWebExtensionState(action.extensionId) {
                    it.copy(allowedInPrivateBrowsing = action.allowed)
                }
            }
            is WebExtensionAction.UpdateBrowserAction -> {
                state.updateWebExtensionState(action.extensionId) {
                    it.copy(browserAction = action.browserAction)
                }
            }
            is WebExtensionAction.UpdatePageAction -> {
                state.updateWebExtensionState(action.extensionId) {
                    it.copy(pageAction = action.pageAction)
                }
            }
            is WebExtensionAction.UpdatePopupSessionAction -> {
                state.updateWebExtensionState(action.extensionId) {
                    it.copy(popupSessionId = action.popupSessionId, popupSession = action.popupSession)
                }
            }
            is WebExtensionAction.UpdateOptionsPageSessionAction -> {
                if (state.extensions.values.any { it.activeOptionsPage != null }) {
                    state
                } else {
                    state.updateWebExtensionState(action.extensionId) {
                        it.copy(
                            activeOptionsPage =
                                ActiveOptionsPage(
                                    instanceId = action.optionsPageInstanceId,
                                    url = action.optionsPageUrl,
                                    name = action.extensionTranslatedName,
                                )
                        )
                    }
                }
            }
            is WebExtensionAction.ClearOptionsPageSession -> {
                state.updateWebExtensionState(action.extensionId) {
                    it.copy(activeOptionsPage = null)
                }
            }
            is WebExtensionAction.UpdateTabBrowserAction -> {
                state.updateWebExtensionTabState(action.sessionId, action.extensionId) {
                    it.copy(browserAction = action.browserAction)
                }
            }
            is WebExtensionAction.UpdateTabPageAction -> {
                state.updateWebExtensionTabState(action.sessionId, action.extensionId) {
                    it.copy(pageAction = action.pageAction)
                }
            }
            is WebExtensionAction.UpdateWebExtensionAction -> {
                state.updateWebExtensionState(action.updatedExtension.id) { action.updatedExtension }
            }
            is WebExtensionAction.UpdateActiveWebExtensionTabAction -> {
                state.copy(activeWebExtensionTabId = action.activeWebExtensionTabId)
            }
            is WebExtensionAction.UpdatePromptRequestWebExtensionAction -> {
                state.copy(webExtensionPromptRequest = action.promptRequest)
            }
            is WebExtensionAction.ConsumePromptRequestWebExtensionAction -> {
                state.copy(webExtensionPromptRequest = null)
            }
            is WebExtensionAction.UpdatePendingExtensionUpdateAction -> {
                state.copy(
                    pendingExtensionUpdates =
                        state.pendingExtensionUpdates + (action.pendingUpdate.extensionId to action.pendingUpdate)
                )
            }
            is WebExtensionAction.RemovePendingExtensionUpdateAction -> {
                state.copy(pendingExtensionUpdates = state.pendingExtensionUpdates - action.extensionId)
            }
            is WebExtensionAction.UpdatePendingExtensionUpdateReviewRequestAction -> {
                state.copy(pendingExtensionUpdateReviewRequest = action.extensionId)
            }
            is WebExtensionAction.ConsumePendingExtensionUpdateReviewRequestAction -> {
                state.copy(pendingExtensionUpdateReviewRequest = null)
            }
        }
    }

    private fun BrowserState.updateWebExtensionTabState(
        tabId: String,
        extensionId: String,
        update: (WebExtensionState) -> WebExtensionState,
    ): BrowserState {
        return copy(
            tabs =
                tabs.updateTabs(tabId) { current ->
                    val existingExtension = current.extensionState[extensionId]
                    val newExtension = extensionId to update(existingExtension ?: WebExtensionState(extensionId))
                    current.copy(extensionState = current.extensionState + newExtension)
                } ?: tabs
        )
    }

    private fun BrowserState.updateWebExtensionState(
        extensionId: String,
        update: (WebExtensionState) -> WebExtensionState,
    ): BrowserState {
        val existingExtension = extensions[extensionId]
        val newExtension = extensionId to update(existingExtension ?: WebExtensionState(extensionId))
        return copy(extensions = extensions + newExtension)
    }
}
