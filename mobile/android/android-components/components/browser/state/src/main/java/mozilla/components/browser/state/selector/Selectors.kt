/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.selector

import java.net.URI
import java.net.URISyntaxException
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.base.log.logger.Logger

// Extension functions for querying and dissecting [BrowserState]

/**
 * The currently selected tab if there's one.
 *
 * Only one [TabSessionState] can be selected at a given time.
 */
val BrowserState.selectedTab: TabSessionState?
    get() = selectedTabId?.let { id -> findTab(id) }

/** The currently selected tab if there's one that is not private. */
val BrowserState.selectedNormalTab: TabSessionState?
    get() = selectedTabId?.let { id -> findNormalTab(id) }

/**
 * Finds and returns the tab with the given id. Returns null if no matching tab could be found.
 *
 * @param tabId The ID of the tab to search for.
 * @return The [TabSessionState] with the provided [tabId] or null if it could not be found.
 */
fun BrowserState.findTab(tabId: String): TabSessionState? {
    return tabs.firstOrNull { it.id == tabId }
}

/**
 * Finds and returns the tab with the given [EngineSession]. Returns null if no matching tab could be found.
 *
 * @param engineSession The engineSession of the tab to search for.
 * @return The [TabSessionState] with the provided [EngineSession] or null if it could not be found.
 */
fun BrowserState.findTab(engineSession: EngineSession): TabSessionState? {
    return tabs.firstOrNull { it.engineState.engineSession == engineSession }
}

/**
 * Finds and returns the Custom Tab with the given id. Returns null if no matching tab could be found.
 *
 * @param tabId The ID of the custom tab to search for.
 * @return The [CustomTabSessionState] with the provided [tabId] or null if it could not be found.
 */
fun BrowserState.findCustomTab(tabId: String): CustomTabSessionState? {
    return customTabs.firstOrNull { it.id == tabId }
}

/**
 * Finds and returns the custom tab with the given [EngineSession]. Returns null if no matching tab could be found.
 *
 * @param engineSession The engineSession of the custom tab to search for.
 * @return The [CustomTabSessionState] with the provided [EngineSession] or null if it could not be found.
 */
fun BrowserState.findCustomTab(engineSession: EngineSession): CustomTabSessionState? {
    return customTabs.firstOrNull { it.engineState.engineSession == engineSession }
}

/**
 * Finds and returns the normal (non-private) tab with the given id. Returns null if no matching tab could be found.
 *
 * @param tabId The ID of the tab to search for.
 * @return The [TabSessionState] with the provided [tabId] or null if it could not be found.
 */
fun BrowserState.findNormalTab(tabId: String): TabSessionState? {
    return normalTabs.firstOrNull { it.id == tabId }
}

/** Finds and returns the [TabSessionState] or [CustomTabSessionState] with the given [tabId]. */
fun BrowserState.findTabOrCustomTab(tabId: String): SessionState? {
    return findTab(tabId) ?: findCustomTab(tabId)
}

/**
 * Finds and returns the [TabSessionState] or [CustomTabSessionState] given [EngineSession]. Returns null if no matching
 * tab could be found.
 *
 * @param engineSession The engineSession of the custom tab to search for.
 * @return The [SessionState] with the provided [EngineSession] or null if it could not be found.
 */
fun BrowserState.findTabOrCustomTab(engineSession: EngineSession): SessionState? {
    return findTab(engineSession) ?: findCustomTab(engineSession)
}

/**
 * Finds and returns the tab with the given id or the selected tab if no id was provided (null). Returns null if no
 * matching tab could be found or if no selected tab exists.
 *
 * @param customTabId An optional ID of a custom tab. If not provided or null then the selected tab will be returned.
 * @return The custom tab with the provided ID or the selected tav if no id was provided.
 */
fun BrowserState.findCustomTabOrSelectedTab(customTabId: String? = null): SessionState? {
    return if (customTabId != null) {
        findCustomTab(customTabId)
    } else {
        selectedTab
    }
}

/**
 * Finds and returns the tab with the given id or the selected tab if no id was provided (null). Returns null if no
 * matching tab could be found or if no selected tab exists.
 *
 * @param tabId An optional ID of a tab. If not provided or null then the selected tab will be returned.
 * @return The custom tab with the provided ID or the selected tav if no id was provided.
 */
fun BrowserState.findTabOrCustomTabOrSelectedTab(tabId: String? = null): SessionState? {
    return if (tabId != null) {
        findTabOrCustomTab(tabId)
    } else {
        selectedTab
    }
}

/**
 * Finds and returns the tab with the given url. Returns null if no matching tab could be found.
 *
 * @param url A mandatory url of the searched tab.
 * @param private Whether to look for a matching private or normal tab
 * @return The tab with the provided url
 */
fun BrowserState.findNormalOrPrivateTabByUrl(url: String, private: Boolean): TabSessionState? {
    return tabs.firstOrNull { tab -> tab.content.url == url && tab.content.private == private }
}

/**
 * Finds and returns the tab with the given url ignoring the fragment identifier part of the url. Returns null if no
 * matching tab could be found.
 *
 * @param url A mandatory url of the searched tab.
 * @param private Whether to look for a matching private or normal tab.
 * @return The tab with the provided url.
 */
fun BrowserState.findNormalOrPrivateTabByUrlIgnoringFragment(
    url: String,
    private: Boolean,
): TabSessionState? {
    return tabs.firstOrNull { tab ->
        isSameUrlIgnoringFragment(tab.content.url, url) && tab.content.private == private
    }
}

/**
 * Gets a list of normal or private tabs depending on the requested type.
 *
 * @param private If true, all private tabs will be returned. If false, all normal tabs will be returned.
 */
fun BrowserState.getNormalOrPrivateTabs(private: Boolean): List<TabSessionState> {
    return tabs.filter { it.content.private == private }
}

/** List of private tabs. */
val BrowserState.privateTabs: List<TabSessionState>
    get() = getNormalOrPrivateTabs(private = true)

/** List of normal (non-private) tabs. */
val BrowserState.normalTabs: List<TabSessionState>
    get() = getNormalOrPrivateTabs(private = false)

/** List of all tabs (normal, private and CustomTabs). */
val BrowserState.allTabs: List<SessionState>
    get() = tabs + customTabs

/** Pending updates of installed extensions that are enabled. */
val BrowserState.pendingExtensionUpdatesNeedingAttention: Map<String, PendingExtensionUpdate>
    get() = pendingExtensionUpdates.filterKeys { extensionId -> extensions[extensionId]?.enabled == true }

/**
 * Returns true if the two urls are the same ignoring the fragment identifier - the string after the # in a url (eg,
 * http://foo/bar#buzz).
 *
 * @param tabUrl A mandatory url of the tab.
 * @param url A mandatory url that's being checked.
 * @return true if the check passes, otherwise false.
 */
private fun isSameUrlIgnoringFragment(tabUrl: String, url: String): Boolean {
    return try {
        val tabUri = URI.create(tabUrl)
        val uri = URI.create(url)
        URI(tabUri.scheme, tabUri.authority, tabUri.path, tabUri.query, null).removeTrailingSlash() ==
            URI(uri.scheme, uri.authority, uri.path, uri.query, null).removeTrailingSlash()
    } catch (e: URISyntaxException) {
        Logger.error("Unable to compare urls", e)
        false
    } catch (e: IllegalArgumentException) {
        Logger.error("Unable to compare urls", e)
        false
    }
}

/**
 * Removes trailing slash on the URI if present.
 *
 * @return String with trailing slash removed if it's the last character, otherwise the original string.
 */
private fun URI.removeTrailingSlash(): String {
    return toString().trimEnd('/')
}
