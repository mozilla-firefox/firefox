/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state

import java.util.Locale
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.concept.awesomebar.AwesomeBar
import mozilla.components.lib.state.State

/**
 * Value type that represents the complete state of the browser/engine.
 *
 * @property tabs the list of open tabs, defaults to an empty list.
 * @property closedTabs the list of recently closed tabs if a [RecentlyClosedMiddleware] is added, defaults to an empty
 *   list.
 * @property selectedTabId the ID of the currently selected (active) tab.
 * @property customTabs the list of custom tabs, defaults to an empty list.
 * @property containers A map of [SessionState.contextId] and their respective container [ContainerState].
 * @property extensions A map of extension IDs and web extensions of all installed web extensions. The extensions here
 *   represent the default values for all [BrowserState.extensions] and can be overridden per [SessionState].
 * @property webExtensionPromptRequest the actual active web extension prompt request.
 * @property pendingExtensionUpdates Extension updates waiting for the user to grant new permissions, by extension ID.
 * @property activeWebExtensionTabId the ID of the tab that is marked active for web extensions to support
 *   tabs.query({active: true}).
 * @property search the state of search for this browser state.
 * @property downloads Downloads ([DownloadState]s) mapped to their IDs.
 * @property undoHistory History of recently closed tabs to support "undo" (Requires UndoMiddleware).
 * @property restoreComplete Whether or not restoring [BrowserState] has completed. This can be used on application
 *   startup e.g. as an indicator that tabs have been restored.
 * @property locale The current locale of the app. Will be null when following the system default.
 * @property awesomeBarState Holds state for interactions with the [AwesomeBar].
 * @property translationEngine Holds translation state that applies to the browser.
 * @property distributionId Holds the distribution Id from [Distribution]
 * @property desktopMode Whether or not browsing is in desktop mode by default for any newly opened tabs.
 * @property recentlyKilledTabs The list of recently killed tabs (limited to 50).
 * @property translationsInitialized Whether or not the translations flow is initialized
 * @property systemPermissionRequestInProgress Whether a system permission request is in progress.
 */
data class BrowserState(
    val tabs: List<TabSessionState> = emptyList(),
    val customTabs: List<CustomTabSessionState> = emptyList(),
    val closedTabs: List<TabState> = emptyList(),
    val selectedTabId: String? = null,
    val containers: Map<String, ContainerState> = emptyMap(),
    val extensions: Map<String, WebExtensionState> = emptyMap(),
    val webExtensionPromptRequest: WebExtensionPromptRequest? = null,
    val pendingExtensionUpdates: Map<String, PendingExtensionUpdate> = emptyMap(),
    val activeWebExtensionTabId: String? = null,
    val downloads: Map<String, DownloadState> = emptyMap(),
    val search: SearchState = SearchState(),
    val undoHistory: UndoHistoryState = UndoHistoryState(),
    val restoreComplete: Boolean = false,
    val locale: Locale? = null,
    val showExtensionsProcessDisabledPrompt: Boolean = false,
    val extensionsProcessDisabled: Boolean = false,
    val awesomeBarState: AwesomeBarState = AwesomeBarState(),
    val translationEngine: TranslationsBrowserState = TranslationsBrowserState(),
    val distributionId: String? = null,
    val desktopMode: Boolean = false,
    val recentlyKilledTabs: LinkedHashSet<String> = LinkedHashSet(),
    val translationsInitialized: Boolean = false,
    val systemPermissionRequestInProgress: Boolean = false,
) : State
