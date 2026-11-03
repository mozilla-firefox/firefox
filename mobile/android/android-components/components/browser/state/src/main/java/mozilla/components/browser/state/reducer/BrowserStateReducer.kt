/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.AppLifecycleAction
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContainerAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CopyInternetResourceAction
import mozilla.components.browser.state.action.CrashAction
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.DebugAction
import mozilla.components.browser.state.action.DefaultDesktopModeAction
import mozilla.components.browser.state.action.DownloadAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ExtensionsProcessAction
import mozilla.components.browser.state.action.HistoryMetadataAction
import mozilla.components.browser.state.action.InitAction
import mozilla.components.browser.state.action.LastAccessAction
import mozilla.components.browser.state.action.ListenToPageAction
import mozilla.components.browser.state.action.LocaleAction
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.action.ReaderAction
import mozilla.components.browser.state.action.RecentlyClosedAction
import mozilla.components.browser.state.action.RestoreCompleteAction
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.action.SystemAction
import mozilla.components.browser.state.action.SystemPermissionRequestAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.action.TrackingProtectionAction
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.action.UndoAction
import mozilla.components.browser.state.action.UpdateDistribution
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.Action

/**
 * Reducers for [BrowserStore].
 *
 * A reducer is a function that receives the current [BrowserState] and an [Action] and then returns a new
 * [BrowserState].
 */
internal object BrowserStateReducer {
    fun reduce(state: BrowserState, action: BrowserAction): BrowserState {
        return when (action) {
            is InitAction -> state
            is AppLifecycleAction -> state
            is RestoreCompleteAction -> state.copy(restoreComplete = true)
            is ContainerAction -> ContainerReducer.reduce(state, action)
            is RecentlyClosedAction -> RecentlyClosedReducer.reduce(state, action)
            is ContentAction -> ContentStateReducer.reduce(state, action)
            is CustomTabListAction -> CustomTabListReducer.reduce(state, action)
            is EngineAction -> EngineStateReducer.reduce(state, action)
            is ReaderAction -> ReaderStateReducer.reduce(state, action)
            is SystemAction -> SystemReducer.reduce(state, action)
            is TabListAction -> TabListReducer.reduce(state, action)
            is TrackingProtectionAction -> TrackingProtectionStateReducer.reduce(state, action)
            is TranslationsAction -> TranslationsStateReducer.reduce(state, action)
            is WebExtensionAction -> WebExtensionReducer.reduce(state, action)
            is MediaSessionAction -> MediaSessionReducer.reduce(state, action)
            is DownloadAction -> DownloadStateReducer.reduce(state, action)
            is SearchAction -> SearchReducer.reduce(state, action)
            is CrashAction -> CrashReducer.reduce(state, action)
            is LastAccessAction -> LastAccessReducer.reduce(state, action)
            is UndoAction -> UndoReducer.reduce(state, action)
            is ShareResourceAction -> ShareResourceStateReducer.reduce(state, action)
            is CopyInternetResourceAction -> CopyInternetResourceStateReducer.reduce(state, action)
            is LocaleAction -> LocaleStateReducer.reduce(state, action)
            is HistoryMetadataAction -> HistoryMetadataReducer.reduce(state, action)
            is DebugAction -> DebugReducer.reduce(state, action)
            is ExtensionsProcessAction -> ExtensionsProcessStateReducer.reduce(state, action)
            is AwesomeBarAction -> AwesomeBarStateReducer.reduce(state, action)
            is UpdateDistribution -> state.copy(distributionId = action.distributionId)
            is DefaultDesktopModeAction -> DesktopModeReducer.reduce(state = state, action = action)
            is SystemPermissionRequestAction -> SystemPermissionReducer.reduce(state, action)
            is ListenToPageAction -> ListenToPageReducer.reduce(state, action)
        }
    }
}

/**
 * Finds the corresponding tab or custom tab in the [BrowserState] and updates it using [update].
 *
 * Consider using the specialized [updateTabState] or [updateCustomTabState] to limit the tabs to be updated if the
 * properties you want changed are not common to both [SessionState] implementations.
 *
 * @param tabId ID of the tab to change.
 * @param update Returns a new version of the tab state. Must be the same class, preferably using
 *   [SessionState.createCopy].
 */
@Suppress("Unchecked_Cast")
internal fun BrowserState.updateTabOrCustomTabState(
    tabId: String,
    update: (SessionState) -> SessionState,
): BrowserState {
    val newTabs = tabs.updateTabs(tabId, update) as List<TabSessionState>?
    if (newTabs != null) return copy(tabs = newTabs)

    val newCustomTabs = customTabs.updateTabs(tabId, update) as List<CustomTabSessionState>?
    if (newCustomTabs != null) return copy(customTabs = newCustomTabs)

    return this
}

/**
 * Finds the corresponding tab in the [BrowserState] and replaces it using [update].
 *
 * This will only update a [TabSessionState] if such exists with the given [tabId]. Consider using the other specialized
 * [updateCustomTabState] method for updating only [CustomTabSessionState] or the general [updateTabOrCustomTabState] to
 * update any tab or custom tab with a given [tabId].
 *
 * @param tabId ID of the tab to change.
 * @param update Returns a new version of [TabSessionState].
 */
internal fun BrowserState.updateTabState(
    tabId: String,
    update: (TabSessionState) -> TabSessionState,
): BrowserState {
    return tabs.updateTabs(tabId, update)?.let {
        copy(tabs = it)
    } ?: this
}

/**
 * Finds the corresponding custom tab in the [BrowserState] and replaces it using [update].
 *
 * This will only update a [CustomTabSessionState] if such exists with the given [tabId]. Consider using the other
 * specialized [updateTabState] method for updating only [TabSessionState] or the general [updateTabOrCustomTabState] to
 * update any tab or custom tab with a given [tabId].
 *
 * @param tabId ID of the tab to change.
 * @param update Returns a new version of [CustomTabSessionState].
 */
internal fun BrowserState.updateCustomTabState(
    tabId: String,
    update: (CustomTabSessionState) -> CustomTabSessionState,
): BrowserState {
    return customTabs.updateTabs(tabId, update)?.let {
        copy(customTabs = it)
    } ?: this
}

/**
 * Finds the corresponding tab in the list and replaces it using [update].
 *
 * @param tabId ID of the tab to change.
 * @param update Returns a new version of the tab state.
 */
internal fun <T : SessionState> List<T>.updateTabs(
    tabId: String,
    update: (T) -> T,
): List<T>? {
    val tabIndex = indexOfFirst { it.id == tabId }
    if (tabIndex == -1) return null

    return subList(0, tabIndex) + update(get(tabIndex)) + subList(tabIndex + 1, size)
}
