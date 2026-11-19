/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import java.util.concurrent.TimeUnit
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.selectedNormalTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import org.mozilla.fenix.components.menu.middleware.getTabUrl
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.tabstray.ext.isNormalTabInactive
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Stories.hasUrlOfInternallyOpenedStory

/** The time until which a tab is considered in-active (in days). */
const val DEFAULT_ACTIVE_DAYS = 14L

/** The maximum time from when a tab was created or accessed until it is considered "inactive". */
val maxActiveTime = TimeUnit.DAYS.toMillis(DEFAULT_ACTIVE_DAYS)

/**
 * Get the last opened normal tab or last tab with in progress media.
 *
 * @param excludedTabIds The IDs of the tabs that are never returned, such as the tabs in closed tab groups.
 * @return A list of the last opened tab or an empty list.
 */
fun BrowserState.asRecentTabs(excludedTabIds: Set<String> = emptySet()): List<RecentTab> =
    lastOpenedNormalTab(excludedTabIds = excludedTabIds)?.let {
        mutableListOf(RecentTab.Tab(it))
    } ?: mutableListOf()

/**
 * Get the selected normal tab or the last accessed normal tab if there is no selected tab or if the selected tab is a
 * private, homepage or excluded tab.
 *
 * @param excludedTabIds The IDs of the tabs that are never returned, such as the tabs in closed tab groups.
 */
fun BrowserState.lastOpenedNormalTab(excludedTabIds: Set<String> = emptySet()): TabSessionState? {
    val isEligible = { tab: TabSessionState -> !tab.isHomepageTab && tab.id !in excludedTabIds }
    return selectedNormalTab?.takeIf(isEligible) ?: normalTabs.filter(isEligible).maxByOrNull { it.lastAccess }
}

private val TabSessionState.isHomepageTab: Boolean
    get() = getTabUrl() == ABOUT_HOME_URL

/**
 * List of all inactive tabs based on [maxActiveTime]. The user may have disabled the feature so for user interactions
 * consider using the [actualInactiveTabs] method or an in place check of the feature status.
 */
val BrowserState.potentialInactiveTabs: List<TabSessionState>
    get() = normalTabs.filter { it.isNormalTabInactive(maxActiveTime) }

/**
 * List of all inactive tabs based on [maxActiveTime]. The result will be always be empty if the user disabled the
 * feature.
 */
fun BrowserState.actualInactiveTabs(settings: Settings): List<TabSessionState> {
    return if (settings.inactiveTabsAreEnabled) {
        potentialInactiveTabs
    } else {
        emptyList()
    }
}

/**
 * Splits all normal tabs into the active and inactive ones, based on [maxActiveTime]. All normal tabs are considered
 * active if the user disabled the inactive tabs feature.
 *
 * @return The active normal tabs (first) and the inactive tabs (second).
 */
fun BrowserState.partitionNormalTabsByActiveTime(
    settings: Settings
): Pair<List<TabSessionState>, List<TabSessionState>> =
    if (settings.inactiveTabsAreEnabled) {
        normalTabs.partition { !it.isNormalTabInactive(maxActiveTime) }
    } else {
        normalTabs to emptyList()
    }

/**
 * Get if there's a browser history item to get back to or if the current URL is of a story from application's
 * homescreen that we should get back to.
 */
fun BrowserState.canGoBackInHistoryOrToStories(): Boolean = selectedTab?.canGoBackInHistoryOrToStories() ?: false

/**
 * Get if there's a browser history item of this tab to get back to or if its URL is of a story from application's
 * homescreen that we should get back to.
 */
fun SessionState.canGoBackInHistoryOrToStories(): Boolean = content.canGoBack || hasUrlOfInternallyOpenedStory()
