/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.blocklist

import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.support.ktx.kotlin.sha1
import org.mozilla.fenix.ext.containsQueryParameters
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.utils.Settings

/**
 * Class for interacting with the blocklist stored in [settings]. The blocklist is a set of SHA1 hashed URLs, which are
 * stripped of protocols and common subdomains like "www" or "mobile".
 *
 * Also used for filtering the sponsored URLs.
 */
class BlocklistHandler(private val settings: Settings) {

    /** Add an URL to the blocklist. The URL will be stripped and hashed, so no pre-formatted is required. */
    fun addUrlToBlocklist(url: String) {
        val updatedBlocklist = settings.homescreenBlocklist + url.stripAndHash()
        settings.homescreenBlocklist = updatedBlocklist
    }

    /** Remove an URL from the blocklist. The URL will be stripped and hashed, so no pre-formatted is required. */
    fun removeUrlFromBlocklist(url: String) {
        val updatedBlocklist = settings.homescreenBlocklist - url.stripAndHash()
        settings.homescreenBlocklist = updatedBlocklist
    }

    /** Filter a list of recent bookmarks by the blocklist. Requires this class to be contextually in a scope. */
    @JvmName("filterRecentBookmark")
    fun List<Bookmark>.filteredByBlocklist(): List<Bookmark> =
        settings.homescreenBlocklist.let { blocklist ->
            filterNot {
                it.url?.let { url -> blocklistContainsUrl(blocklist, url) } ?: false
            }
        }

    /** Filter a list of recent tabs by the blocklist. Requires this class to be contextually in a scope. */
    @JvmName("filterRecentTab")
    fun List<RecentTab>.filteredByBlocklist(): List<RecentTab> =
        settings.homescreenBlocklist.let { blocklist ->
            filterNot {
                it is RecentTab.Tab && blocklistContainsUrl(blocklist, it.state.content.url)
            }
        }

    /** Filter a list of recent tabs from sponsored urls. */
    @JvmName("filterContileRecentTab")
    fun List<RecentTab>.filterContile(): List<RecentTab> = filterNot {
        it is RecentTab.Tab && it.state.content.url.toUri().containsQueryParameters(settings.frecencyFilterQuery)
    }

    /**
     * If the state is set to [RecentSyncedTabState.Success], filter the list of recently synced tabs by the blocklist.
     * If the filtered list of tabs is empty, change the state to [RecentSyncedTabState.None]
     */
    @JvmName("filterRecentSyncedTabState")
    fun RecentSyncedTabState.filteredByBlocklist() =
        if (this is RecentSyncedTabState.Success) {
            val filteredTabs =
                settings.homescreenBlocklist.let { blocklist ->
                    this.tabs.filterNot {
                        blocklistContainsUrl(blocklist, it.url)
                    }
                }
            if (filteredTabs.isEmpty()) {
                RecentSyncedTabState.None
            } else {
                RecentSyncedTabState.Success(filteredTabs)
            }
        } else {
            this
        }

    /** Filter a list of recently synced tabs of sponsored urls if the state is [RecentSyncedTabState.Success]. */
    @JvmName("filterContileRecentSyncedTab")
    fun RecentSyncedTabState.filterContile() =
        if (this is RecentSyncedTabState.Success) {
            val filteredTabs =
                this.tabs.filterNot {
                    it.url.toUri().containsQueryParameters(settings.frecencyFilterQuery)
                }
            if (filteredTabs.isEmpty()) {
                RecentSyncedTabState.None
            } else {
                RecentSyncedTabState.Success(filteredTabs)
            }
        } else {
            this
        }

    /** Filter a list of recent history items by the blocklist. Requires this class to be contextually in a scope. */
    @JvmName("filterRecentHistory")
    fun List<RecentlyVisitedItem>.filteredByBlocklist(): List<RecentlyVisitedItem> =
        settings.homescreenBlocklist.let { blocklist ->
            filterNot {
                it is RecentlyVisitedItem.RecentHistoryHighlight && blocklistContainsUrl(blocklist, it.url)
            }
        }

    /** Filter a list of recently visited history items of sponsored urls. */
    @JvmName("filterContileRecentlyVisited")
    fun List<RecentlyVisitedItem>.filterContile(): List<RecentlyVisitedItem> = filterNot {
        it is RecentlyVisitedItem.RecentHistoryHighlight &&
            it.url.toUri().containsQueryParameters(settings.frecencyFilterQuery)
    }

    /**
     * Filter a list of top sites by the blocklist. Requires this class to be contextually in a scope.
     *
     * This is intended for the pool of default top sites provided by the application and does not apply to user-pinned
     * top sites.
     */
    @JvmName("filterTopSite")
    fun <T : TopSite> List<T>.filteredByBlocklist(): List<T> =
        settings.homescreenBlocklist.let { blocklist -> filterNot { blocklistContainsUrl(blocklist, it.url) } }

    private fun blocklistContainsUrl(blocklist: Set<String>, url: String): Boolean = blocklist.any {
        it == url.stripAndHash()
    }
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun String.stripAndHash(): String = this.stripProtocolAndCommonSubdomains().sha1()

// Eventually, this should be standardize in A-C and this can then be removed
// https://github.com/mozilla-mobile/android-components/issues/11743
private fun String.stripProtocolAndCommonSubdomains(): String {
    val stripped = this.substringAfter("://").dropLastWhile { it == '/' }
    // This kind of stripping allows us to match "twitter" to "mobile.twitter.com".
    // Borrowed from DomainMatcher in A-C
    val domainsToStrip = listOf("www", "mobile", "m")

    domainsToStrip.forEach { domain ->
        if (stripped.startsWith("$domain.")) {
            return stripped.substring(domain.length + 1)
        }
    }
    return stripped
}

/** Whether this URL and [url] are the same entry in the blocklist. */
internal fun String.hasSameBlocklistEntryAs(url: String): Boolean =
    stripProtocolAndCommonSubdomains() == url.stripProtocolAndCommonSubdomains()
